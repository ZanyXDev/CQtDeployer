/*
 * Copyright (C) 2018-2019 QuasarApp.
 * Distributed under the lgplv3 software license, see the accompanying
 * Everyone is permitted to copy and distribute verbatim copies
 * of this license document, but changing it is not allowed.
 */

#include "deploy.h"
#include "deploycore.h"
#include "pluginsparser.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <quasarapp.h>
#include <stdio.h>


#include <fstream>


bool Deploy::getDeployQml() const { return deployQml; }

void Deploy::setDeployQml(bool value) { deployQml = value; }

QString Deploy::getQmlScaner() const { return externQmlScaner; }

QString Deploy::getQmake() const { return qmake; }

void Deploy::clear(bool force) {
    _fileManager.clear(targetDir, force);
}

bool Deploy::deployMSVC() {
    qInfo () << "try deploy msvc";

    auto msvcInstaller = DeployCore::getVCredist(qmake);

    if (msvcInstaller.isEmpty()) {
        return false;
    }

    return _fileManager.copyFile(msvcInstaller, targetDir);
}

bool Deploy::createRunScript(const QString &target) {

    QString content =
            "#!/bin/sh\n"
            "BASE_DIR=$(dirname \"$(readlink -f \"$0\")\")\n"
            "export "
            "LD_LIBRARY_PATH=\"$BASE_DIR\"/lib:\"$BASE_DIR\":$LD_LIBRARY_PATH\n"
            "export QML_IMPORT_PATH=\"$BASE_DIR\"/qml:QML_IMPORT_PATH\n"
            "export QML2_IMPORT_PATH=\"$BASE_DIR\"/qml:QML2_IMPORT_PATH\n"
            "export QT_PLUGIN_PATH=\"$BASE_DIR\"/plugins:QT_PLUGIN_PATH\n"
            "export QTDIR=\"$BASE_DIR\"\n"
            "export "
            "QT_QPA_PLATFORM_PLUGIN_PATH=\"$BASE_DIR\"/plugins/"
            "platforms:QT_QPA_PLATFORM_PLUGIN_PATH\n"
            "%2"
            "\"$BASE_DIR\"/bin/%1 \"$@\"";

    content = content.arg(QFileInfo(target).fileName());
    int ld_index = find("ld-linux", _fileManager.getDeployedFilesStringList());

    if (ld_index >= 0 && QuasarAppUtils::Params::isEndable("deploySystem") &&
            !QuasarAppUtils::Params::isEndable("noLibc")) {

        content = content.arg(QString("\nexport LD_PRELOAD=\"$BASE_DIR\"/lib/%0\n").
            arg(QFileInfo(_fileManager.getDeployedFilesStringList()[ld_index]).fileName()));
    } else {
        content = content.arg("");
    }


    QString fname = targetDir + QDir::separator() + QFileInfo(target).baseName()+ ".sh";

    QFile F(fname);
    if (!F.open(QIODevice::WriteOnly)) {
        return false;
    }

    F.write(content.toUtf8());
    F.flush();
    F.close();

    _fileManager.addToDeployed(fname);

    return F.setPermissions(QFileDevice::ExeOther | QFileDevice::WriteOther |
                            QFileDevice::ReadOther | QFileDevice::ExeUser |
                            QFileDevice::WriteUser | QFileDevice::ReadUser |
                            QFileDevice::ExeOwner | QFileDevice::WriteOwner |
                            QFileDevice::ReadOwner);
}

bool Deploy::createQConf() {

    QString content =
            "[Paths]\n"
            "Prefix= ./\n"
            "Libraries= ./\n"
            "Plugins= ./plugins\n"
            "Imports= ./qml\n"
            "Qml2Imports= ./qml\n";


    QString fname = targetDir + QDir::separator() + "qt.conf";

    QFile F(fname);
    if (!F.open(QIODevice::WriteOnly)) {
        return false;
    }

    F.write(content.toUtf8());
    F.flush();
    F.close();

    _fileManager.addToDeployed(fname);

    return F.setPermissions(QFileDevice::ExeOther | QFileDevice::WriteOther |
                            QFileDevice::ReadOther | QFileDevice::ExeUser |
                            QFileDevice::WriteUser | QFileDevice::ReadUser |
                            QFileDevice::ExeOwner | QFileDevice::WriteOwner |
                            QFileDevice::ReadOwner);
}



void Deploy::deploy() {
    qInfo() << "target deploy started!!";

    smartMoveTargets();

    for (auto i = targets.cbegin(); i != targets.cend(); ++i) {
        extract(i.key());
    }

    if (deployQml && !extractQml()) {
        qCritical() << "qml not extacted!";
    }

    PluginsParser pluginsParser(&scaner);

    QStringList plugins;
    pluginsParser.scan(DeployCore::qtDir + "/plugins", plugins);
    copyPlugins(plugins);


    _fileManager.copyFiles(neadedLibs, targetDir);

    if (QuasarAppUtils::Params::isEndable("deploySystem")) {
        _fileManager.copyFiles(systemLibs, targetDir);
    }

    if (!QuasarAppUtils::Params::isEndable("noStrip") && !_fileManager.strip(targetDir)) {
        QuasarAppUtils::Params::verboseLog("strip failed!");
    }

    if (!QuasarAppUtils::Params::isEndable("noTranslations")) {
        if (!copyTranslations(DeployCore::extractTranslation(neadedLibs))) {
            qWarning() << " copy TR ERROR";
        }
    }

    if (!deployMSVC()) {
        QuasarAppUtils::Params::verboseLog("deploy msvc failed");
    }

    bool targetWindows = false;

    for (auto i = targets.cbegin(); i != targets.cend(); ++i) {

        if (QFileInfo(i.key()).completeSuffix() == "exe") {
            targetWindows = true;
        }

        if (i.value() && !createRunScript(i.key())) {
            qCritical() << "run script not created!";
        }
    }

    if (targetWindows && !createQConf()) {
        QuasarAppUtils::Params::verboseLog("create qt.conf failr", QuasarAppUtils::Warning);
    }

    _fileManager.saveDeploymendFiles(targetDir);

    qInfo() << "deploy done!";


}

QString Deploy::getQtDir() const { return DeployCore::qtDir; }

void Deploy::setDepchLimit(int value) { depchLimit = value; }

int Deploy::find(const QString &str, const QStringList &list) const {
    for (int i = 0 ; i < list.size(); ++i) {
        if (list[i].contains(str))
            return i;
    }
    return -1;
}

bool Deploy::copyPlugin(const QString &plugin) {

    QStringList listItems;

    if (!_fileManager.copyFolder(plugin, targetDir + "/plugins/" + QFileInfo(plugin).fileName(),
                    QStringList() << ".so.debug" << "d.dll", &listItems)) {
        return false;
    }

    for (auto item : listItems) {
        extract(item);
    }

    return true;
}

void Deploy::copyPlugins(const QStringList &list) {
    for (auto plugin : list) {
        if (!copyPlugin(plugin)) {
            qWarning() << plugin << " not copied!";
        }
    }
    QFileInfo info;

    for (auto extraPlugin : extraPlugins) {

        info.setFile(extraPlugin);
        if (info.isDir()) {

            _fileManager.copyFolder(info.absoluteFilePath(),
                       targetDir + "/plugins/" + info.baseName(),
                       QStringList() << ".so.debug" << "d.dll");
        } else {
            _fileManager.copyFile(info.absoluteFilePath(),
                     targetDir + QDir::separator() + "plugins");
            extract(info.absoluteFilePath());
        }
    }
}

bool Deploy::copyTranslations(QStringList list) {

    QDir dir(translationDir);
    if (!dir.exists() || list.isEmpty()) {
        return false;
    }

    QStringList filters;
    for (auto &&i: list) {
        filters.push_back("*" + i + "*");
    }

    auto listItems = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot);

    for (auto &&i: listItems) {
        _fileManager.copyFile(i.absoluteFilePath(), targetDir + "/translations");
    }

    return true;
}



QFileInfoList Deploy::findFilesInsideDir(const QString &name,
                                         const QString &dirpath) {
    QFileInfoList files;

    QDir dir(dirpath);

    auto list = dir.entryInfoList( QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (auto && item :list) {
        if (item.isFile()) {
            if (item.fileName().contains(name)) {
                files += item;
            }
        } else {
            files += findFilesInsideDir(name, item.absoluteFilePath());
        }
    }

    return files;
}

QString Deploy::filterQmlPath(const QString &path) {
    if (path.contains(qmlDir)) {
        auto endIndex = path.indexOf(QDir::separator(), qmlDir.size() + 1);
        QString module =
                path.mid(qmlDir.size() + 1, endIndex - qmlDir.size() - 1);
        return qmlDir + QDir::separator() + module;
    }

    return "";
}

void Deploy::extractLib(const QString &file) {
    qInfo() << "extract lib :" << file;

    auto data = scaner.scan(file);

    for (auto &line : data) {
        bool isIgnore = false;
        for (auto ignore : ignoreList) {
            if (line.fullPath().contains(ignore)) {
                QuasarAppUtils::Params::verboseLog(line.fullPath() + " ignored by filter" + ignore);
                isIgnore = true;
                continue;
            }
        }

        if (isIgnore) {
            continue;
        }

        if (line.getPriority() != LibPriority::SystemLib && !neadedLibs.contains(line.fullPath())) {
            neadedLibs << line.fullPath();
        } else if (QuasarAppUtils::Params::isEndable("deploySystem") &&
                    line.getPriority() == LibPriority::SystemLib &&
                    !systemLibs.contains(line.fullPath())) {
            systemLibs << line.fullPath();
        }
    }
}

void Deploy::addEnv(const QString &dir) {

    char separator = ':';

#ifdef Q_OS_WIN
    separator = ';';
#endif

    if (dir.contains(separator)) {
        auto list = dir.split(separator);
        for (auto i : list) {
            addEnv(i);
        }
        return;
    }

    auto path = QFileInfo(dir).absoluteFilePath();

    for (QString & i :ignoreEnvList) {
        if (path.contains(i)) {
            return;
        }
    }

    if (path.contains(appDir)) {
        QuasarAppUtils::Params::verboseLog("is cqtdeployer dir!: " + path + " app dir : " + appDir);
        return;
    }

    if (!QFileInfo(path).isDir()) {
        QuasarAppUtils::Params::verboseLog("is not dir!! :" + path);
        return;
    }

    if (deployEnvironment.contains(path)) {
        QuasarAppUtils::Params::verboseLog ("Environment alredy added: " + path);
        return;
    }

    if (path.contains(targetDir)) {
        QuasarAppUtils::Params::verboseLog ("Skip paths becouse it is target : " + path);
        return;
    }

    deployEnvironment.push_back(QDir::fromNativeSeparators(path));
}

QString Deploy::concatEnv() const {

    if (deployEnvironment.isEmpty()) {
        return "";
    }

    QString result = deployEnvironment.first();
    for (auto i: deployEnvironment) {
        result += (":" + i);
    }

    return result;
}

bool Deploy::smartMoveTargets() {

    QMap<QString, bool> temp;
    bool result = true;
    for (auto i = targets.cbegin(); i != targets.cend(); ++i) {

        QFileInfo target(i.key());
        auto targetPath = targetDir + (isLib(target) ? "/lib" : "/bin");

        if (target.completeSuffix().contains("dll", Qt::CaseInsensitive) ||
                target.completeSuffix().contains("exe", Qt::CaseInsensitive)) {

            targetPath = targetDir;

        }

        if (!_fileManager.smartCopyFile(target.absoluteFilePath(), targetPath, targetDir)) {
            result = false;
        }


        temp.insert(targetPath + "/" + target.fileName(), i.value());

    }

    targets = temp;

    scaner.setEnvironment(deployEnvironment);

    return result;
}

bool Deploy::isLib(const QFileInfo &file) {
    return file.completeSuffix().contains("so", Qt::CaseInsensitive)
            || file.completeSuffix().contains("dll", Qt::CaseInsensitive);
}

QStringList Deploy::extractImportsFromDir(const QString &filepath) {
    QProcess p;

    QProcessEnvironment env;

    env.insert("LD_LIBRARY_PATH", concatEnv());
    env.insert("QML_IMPORT_PATH", DeployCore::qtDir + "/qml");
    env.insert("QML2_IMPORT_PATH", DeployCore::qtDir + "/qml");
    env.insert("QT_PLUGIN_PATH", DeployCore::qtDir + "/plugins");
    env.insert("QT_QPA_PLATFORM_PLUGIN_PATH", DeployCore::qtDir + "/plugins/platforms");

    p.setProcessEnvironment(env);
    p.setProgram(externQmlScaner);
    p.setArguments(QStringList()
                   << "-rootPath" << filepath << "-importPath" << qmlDir);
    p.start();

    if (!p.waitForFinished()) {
        qWarning() << filepath << " not scaning!";
        return QStringList();
    }

    auto rawData = p.readAll();

    if (p.exitCode()) {
        qWarning() << "scaner error " << p.errorString() << "exitCode: " << p.exitCode();
    }

    QuasarAppUtils::Params::verboseLog("rawData from extractImportsFromDir: " + rawData);

    auto data = QJsonDocument::fromJson(rawData);

    if (!data.isArray()) {
        qWarning() << "wrong data from qml scaner! of " << filepath;
    }

    auto array = data.array();

    QStringList result;

    for (auto object : array) {

        auto module = object.toObject().value("path").toString();

        if (module.isEmpty()) {
            continue;
        }

        if (!result.contains(module)) {
            result << module;
        }
    }

    return result;
}

bool Deploy::extractQmlAll() {

    if (!QFileInfo::exists(qmlDir)) {
        qWarning() << "qml dir wrong!";
        return false;
    }

    QStringList listItems;

    if (!_fileManager.copyFolder(qmlDir, targetDir + "/qml",
                    QStringList() << ".so.debug" << "d.dll",
                    &listItems)) {
        return false;
    }

    for (auto item : listItems) {
        extract(item);
    }

    return true;
}

bool Deploy::extractQmlFromSource(const QString& sourceDir) {

    QFileInfo info(sourceDir);

    if (!info.isDir()) {
        qCritical() << "extract qml fail! qml source dir not exits or is not dir " << sourceDir;
        return false;
    }

    QuasarAppUtils::Params::verboseLog("extractQmlFromSource " + info.absoluteFilePath());

    if (!QFileInfo::exists(qmlDir)) {
        qWarning() << "qml dir wrong!";
        return false;
    }

    QStringList plugins;
    QStringList listItems;
    QStringList filter;
    filter << ".so.debug" << "d.dll" << ".pdb";

    if (QuasarAppUtils::Params::isEndable("qmlExtern")) {

        ///  @todo remove in verison 1.3
        qInfo() << "use extern qml scaner!";

        plugins = extractImportsFromDir(info.absoluteFilePath());

    } else {
        qInfo() << "use own qml scaner!";

        QML ownQmlScaner(qmlDir);

        if (!ownQmlScaner.scan(plugins, info.absoluteFilePath())) {
            QuasarAppUtils::Params::verboseLog("qml scaner run failed!");
            return false;
        }
    }

    if (!_fileManager.copyFolder(qmlDir, targetDir + "/qml",
                    filter , &listItems, &plugins)) {
        return false;
    }

    for (auto item : listItems) {
        extract(item);
    }

    return true;
}

bool Deploy::extractQml() {

    if (QuasarAppUtils::Params::isEndable("qmlDir")) {
        return extractQmlFromSource(
                    QuasarAppUtils::Params::getStrArg("qmlDir"));

    } else if (QuasarAppUtils::Params::isEndable("allQmlDependes")) {
        return extractQmlAll();

    } else {
        return false;
    }
}

void Deploy::extract(const QString &file) {
    QFileInfo info(file);

    auto sufix = info.completeSuffix();

    if (sufix.contains("dll", Qt::CaseSensitive) ||
            sufix.contains("exe", Qt::CaseSensitive) ||
            sufix.isEmpty() || sufix.contains("so", Qt::CaseSensitive)) {

        extractLib(file);
    } else {
        QuasarAppUtils::Params::verboseLog("file with sufix " + sufix + " not supported!");
    }

}

Deploy::Deploy() {
#ifdef Q_OS_LINUX
    appDir = QuasarAppUtils::Params::getStrArg("appPath");

    if (appDir.right(4) == "/bin") {
        appDir = appDir.left(appDir.size() - 4);
    }
#else
    appDir = QuasarAppUtils::Params::getStrArg("appPath");
#endif

    QuasarAppUtils::Params::verboseLog("appDir = " + appDir);

}

