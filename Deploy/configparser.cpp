//#
//# Copyright (C) 2018-2020 QuasarApp.
//# Distributed under the lgplv3 software license, see the accompanying
//# Everyone is permitted to copy and distribute verbatim copies
//# of this license document, but changing it is not allowed.
//#

#include "configparser.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include "dependenciesscanner.h"
#include "deploycore.h"
#include "filemanager.h"
#include "packing.h"
#include "pathutils.h"
#include "quasarapp.h"

#include <cassert>

#include <Distributions/defaultdistro.h>
#include <Distributions/qif.h>
/**
 * this function init packages of project
 * inputParamsList - list of parameters
 *                   patern : value;package
 * mainContainer - container for insert data, usually it is packages map.
 * seterFunc - this is method of item of mainConteiner for set value from inputParamsList
 * important : package in inputParamsList must be second.
 */

static QString defaultPackage = "";

template<typename Container, typename Setter>
bool parsePackagesPrivate(Container& mainContainer,
                          const QStringList &inputParamsList,
                          Setter setter) {

    for (const auto& str: inputParamsList) {
        auto pair = str.split(DeployCore::getSeparator(1));
        auto first = pair.value(0, "");
        auto second = pair.value(1, "");
        if (pair.size() == 1)
            (mainContainer[defaultPackage].*setter)(first);
        else {
            first = PathUtils::fullStripPath(first);
            if (!mainContainer.contains(first)) {
                return false;
            }

            (mainContainer[first].*setter)(second);

        }

    }

    return true;
}

bool ConfigParser::parseParams() {

    auto path = QuasarAppUtils::Params::getStrArg("confFile");
    bool createFile = !QFile::exists(path) &&
            QuasarAppUtils::Params::isEndable("confFile");

    if (path.isEmpty() &&
            QuasarAppUtils::Params::customParamasSize() <= 0) {
        path = DEFAULT_COFIGURATION_FILE;
    }

    if (QFile::exists(path)) {
        if (!loadFromFile(path)) {
            QuasarAppUtils::Params::log("failed to parse " + path,
                                               QuasarAppUtils::Error);
            return false;
        }
    }

    auto distro = getDistribution();
    _packing->setDistribution(distro);

    switch (DeployCore::getMode()) {
    case RunMode::Info: {
        QuasarAppUtils::Params::log("Print info ...",
                                           QuasarAppUtils::Info);

        if (!parseInfoMode()) {
            QuasarAppUtils::Params::log("show info is failed!",
                                               QuasarAppUtils::Error);
            return false;
        }
        break;
    }
    case RunMode::Clear: {
        QuasarAppUtils::Params::log("clear ...",
                                           QuasarAppUtils::Info);

        if (!parseClearMode()) {
            QuasarAppUtils::Params::log("clear is failed!",
                                               QuasarAppUtils::Error);
            return false;
        }
        break;
    }

    case RunMode::Init: {
        QuasarAppUtils::Params::log("Init ...",
                                           QuasarAppUtils::Info);
        if (!parseInitMode()) {
            QuasarAppUtils::Params::log("init is failed!",
                                               QuasarAppUtils::Error);
            return false;
        }
        break;
    }

    case RunMode::Deploy: {
        QuasarAppUtils::Params::log("Deploy ...",
                                           QuasarAppUtils::Info);

        if (!parseDeployMode()) {
            QuasarAppUtils::Params::log("deploy is failed!",
                                               QuasarAppUtils::Error);
            return false;
        }
        break;
    }

    }

    DeployCore::_config = &_config;

    if (createFile && !createFromDeploy(path)) {
        QuasarAppUtils::Params::log("Do not create a deploy config file in " + path,
                                           QuasarAppUtils::Error);
    }

    return true;
}

const DeployConfig *ConfigParser::config() const {
    return &_config;
}

// FIX ME. if package contains the path separators then package rewrite to RelativeLink of configFile location
QJsonValue ConfigParser::writeKeyArray(int separatorLvl, const QString &parameter,
                                 const QString &confFileDir) const {

    auto list = parameter.split(DeployCore::getSeparator(separatorLvl));

    if (DeployCore::isContainsArraySeparators(parameter)) {
        QJsonArray array;

        for (const auto &i: list) {
            array.push_back(writeKeyArray(separatorLvl + 1, i, confFileDir));
        }

        return array;
    }

    if (list.size() && list.first().isEmpty()) {
        return QJsonValue(true);
    }

    auto val = list.first();

    if (PathUtils::isPath(val)) {

        val = PathUtils::getRelativeLink(
                    QFileInfo(confFileDir).absoluteFilePath(),
                    QFileInfo(val).absoluteFilePath());

    }

    return val;

}

void ConfigParser::writeKey(const QString& key, QJsonObject& obj,
                            const QString& confFileDir) const {
    if (QuasarAppUtils::Params::isEndable(key)) {
        obj[key] = writeKeyArray(0, QuasarAppUtils::Params::getStrArg(key), confFileDir);
    }
}

QString ConfigParser::readKeyArray(int separatorLvl, const QJsonArray &array,
                                       const QString& confFileDir) const {

    QStringList list;

    for (const QJsonValue &i : array) {

        if (i.isArray()) {
            list.push_back(readKeyArray(separatorLvl + 1, i.toArray(), confFileDir));
        } else {
            QString val = i.toString();

            if (i.type() == QJsonValue::Double) {
                val = QString::number(i.toDouble(0), 'f');
            }

            if (!val.isEmpty()) {
                if (PathUtils::isReleativePath(val)) {
                    list.push_back(QFileInfo(confFileDir + '/' + val).absoluteFilePath());
                } else {
                    list.push_back(val);
                }
            }
        }
    }

    return list.join(DeployCore::getSeparator(separatorLvl));
}

void ConfigParser::readKey(const QString& key, const QJsonObject& obj,
                           const QString& confFileDir) const {

    if (!QuasarAppUtils::Params::isEndable(key)) {
        auto type = obj[key].type();

        switch (type) {
        case QJsonValue::Array: {
            auto array = obj[key].toArray();
            QuasarAppUtils::Params::setArg(key, readKeyArray(0, array, confFileDir));
            break;
        }
        case QJsonValue::Double: {
            readString(key,
                       QString::number(obj[key].toDouble(0), 'f'),
                       confFileDir);

            break;
        }
        case QJsonValue::String: {
            readString(key,
                       obj[key].toString(),
                       confFileDir);
            break;
        }
        default: {
            auto value = obj[key].toBool(true);
            QuasarAppUtils::Params::setEnable(key, value);
            break;
        }
        }

    }
}

void ConfigParser::readString(const QString &key, const QString &val,
                              const QString& confFileDir) const
{
    if (PathUtils::isReleativePath(val)) {
        QuasarAppUtils::Params::setArg(key, QFileInfo(confFileDir + '/' + val).absoluteFilePath());
    } else {
        QuasarAppUtils::Params::setArg(key, val);
    }
}

bool ConfigParser::createFromDeploy(const QString& confFile) const {
    QJsonObject obj;

    auto info = QFileInfo(confFile);

    for (const auto &key :DeployCore::helpKeys()) {
        writeKey(key, obj, info.absolutePath());
    }

    if (!QFile::exists(info.absolutePath()) &&
            !QDir("/").mkpath(info.absolutePath())) {
        return false;
    }

    QJsonDocument doc(obj);

    QFile file(confFile);

    if (file.open(QIODevice::WriteOnly| QIODevice::Truncate)) {
        file.write(doc.toJson());
        file.close();

        return true;
    }

    return false;

}

bool ConfigParser::loadFromFile(const QString& confFile) {
    QFile file(confFile);
    QString confFilePath = QFileInfo(confFile).absolutePath();

    if (file.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(file.readAll());

        if (!doc.isObject()) {
            return false;
        }

        auto obj = doc.object();

        for (const auto &key: obj.keys()) {
            readKey(key, obj, confFilePath);
        }

        file.close();

        return true;
    }

    return false;
}

bool ConfigParser::initDistroStruct() {

    if (!initPackages()) {
        return false;
    }

    auto &mainDistro = _config.packagesEdit();

#ifdef Q_OS_LINUX

    auto binOut = QuasarAppUtils::Params::getStrArg("binOut").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto libOut = QuasarAppUtils::Params::getStrArg("libOut").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);

#else
    auto binOut = QuasarAppUtils::Params::getStrArg("binOut").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto libOut = QuasarAppUtils::Params::getStrArg("libOut").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
#endif

    auto qmlOut = QuasarAppUtils::Params::getStrArg("qmlOut").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto trOut = QuasarAppUtils::Params::getStrArg("trOut").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto pluginOut = QuasarAppUtils::Params::getStrArg("pluginOut").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto recOut = QuasarAppUtils::Params::getStrArg("recOut").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);

    auto name = QuasarAppUtils::Params::getStrArg("name").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto description = QuasarAppUtils::Params::getStrArg("description").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto deployVersion = QuasarAppUtils::Params::getStrArg("deployVersion").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto releaseDate = QuasarAppUtils::Params::getStrArg("releaseDate").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto icon = QuasarAppUtils::Params::getStrArg("icon").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);
    auto publisher = QuasarAppUtils::Params::getStrArg("publisher").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);

    auto erroLog = [](const QString &flag){
            QuasarAppUtils::Params::log(QString("Set %0 fail, because you try set %0 for not inited package."
                                               " Use 'targetPackage' flag for init the packages").arg(flag),
                                               QuasarAppUtils::Error);
        };

// init distro stucts for all targets
    if (binOut.size() && !parsePackagesPrivate(mainDistro, binOut, &DistroModule::setBinOutDir)) {
        erroLog("binOut");
        return false;
    }

    if (libOut.size() && !parsePackagesPrivate(mainDistro, libOut, &DistroModule::setLibOutDir)) {
        erroLog("libOut");
        return false;
    }

    if (qmlOut.size() && !parsePackagesPrivate(mainDistro, qmlOut, &DistroModule::setQmlOutDir)) {
        erroLog("qmlOut");
        return false;
    }

    if (trOut.size() && !parsePackagesPrivate(mainDistro, trOut, &DistroModule::setTrOutDir)) {
        erroLog("trOut");
        return false;
    }

    if (pluginOut.size() && !parsePackagesPrivate(mainDistro, pluginOut, &DistroModule::setPluginsOutDir)) {
        erroLog("pluginOut");
        return false;
    }

    if (recOut.size() && !parsePackagesPrivate(mainDistro, recOut, &DistroModule::setResOutDir)) {
        erroLog("recOut");
        return false;
    }

    if (name.size() && !parsePackagesPrivate(mainDistro, name, &DistroModule::setName)) {
        erroLog("name");
        return false;
    }

    if (description.size() && !parsePackagesPrivate(mainDistro, description, &DistroModule::setDescription)) {
        erroLog("description");
        return false;
    }

    if (deployVersion.size() && !parsePackagesPrivate(mainDistro, deployVersion, &DistroModule::setVersion)) {
        erroLog("deployVersion");
        return false;
    }

    if (releaseDate.size() && !parsePackagesPrivate(mainDistro, releaseDate, &DistroModule::setReleaseData)) {
        erroLog("releaseDate");
        return false;
    }

    if (icon.size() && !parsePackagesPrivate(mainDistro, icon, &DistroModule::setIcon)) {
        erroLog("icon");
        return false;
    }

    if (publisher.size() && !parsePackagesPrivate(mainDistro, publisher, &DistroModule::setPublisher)) {
        erroLog("Publisher");
        return false;
    }

    return true;
}

bool ConfigParser::initPackages() {

    defaultPackage = "";

    if (QuasarAppUtils::Params::isEndable("targetPackage")) {
        auto tar_packages_array = QuasarAppUtils::Params::getStrArg("targetPackage", "").
                split(DeployCore::getSeparator(0));


        QSet<QString> configuredTargets;
        for (auto& str: tar_packages_array) {
            auto pair = str.split(DeployCore::getSeparator(1));
            auto package = PathUtils::fullStripPath(pair.value(0, ""));

            auto list = _config.getTargetsListByFilter(pair.value(1, ""));

            for (auto it = list.begin(); it != list.end(); ++it) {
                if (!configuredTargets.contains(it.key())) {
                    configuredTargets.insert(it.key());
                    it.value()->setPackage(package);
                }
            }

            _config.packagesEdit().insert(package, {});

            if (pair.size() != 2) {
                defaultPackage = package;
            }
        }

        QuasarAppUtils::Params::log(
                    "Set Default Package to " + defaultPackage,
                     QuasarAppUtils::Info);
    }

    return true;
}

bool ConfigParser::initQmlInput() {

    auto qmlDir = QuasarAppUtils::Params::getStrArg("qmlDir").
            split(DeployCore::getSeparator(0), QString::SkipEmptyParts);

    if (QuasarAppUtils::Params::isEndable("allQmlDependes")) {
        _config.deployQml = true;
        return true;
    }

    auto erroLog = [](const QString &flag){
            QuasarAppUtils::Params::log(QString("Set %0 fail, because you try set %0 for not inited package."
                                               " Use 'targetPackage' flag for init the packages").arg(flag),
                                               QuasarAppUtils::Error);
        };

// init distro stucts for all targets
    _config.deployQml = qmlDir.size();

    if (qmlDir.size() && !parsePackagesPrivate(_config.packagesEdit(), qmlDir, &DistroModule::addQmlInput)) {
        erroLog("qmlDir");
        return false;
    }

    return true;
}

bool ConfigParser::parseDeployMode() {

    if (QuasarAppUtils::Params::isEndable("deploySystem-with-libc")) {
        QuasarAppUtils::Params::setEnable("deploySystem", true );
    }

    auto bin = QuasarAppUtils::Params::getStrArg("bin").
            split(DeployCore::getSeparator(0));

    if (!setTargets(bin)) {

        auto binDir = QuasarAppUtils::Params::getStrArg("binDir");
        if (!setTargetsRecursive(binDir)) {
            QuasarAppUtils::Params::log("setTargetDir fail!",
                                               QuasarAppUtils::Error);
            return false;
        }
    }

    _config.depchLimit = 0;

    if (QuasarAppUtils::Params::isEndable("recursiveDepth")) {
        bool ok;
        _config.depchLimit = QuasarAppUtils::Params::getArg("recursiveDepth").toInt(&ok);
        if (!ok) {
            _config.depchLimit = 0;
            QuasarAppUtils::Params::log("recursiveDepth is invalid! use default value 0",
                                               QuasarAppUtils::Warning);
        }
    }


    initIgnoreEnvList();
    initEnvirement();
    initIgnoreList();
    if (!initDistroStruct()) {
        return false;
    }

    auto listLibDir = QuasarAppUtils::Params::getStrArg("libDir").
            split(DeployCore::getSeparator(0));
    auto listNamesMasks = QuasarAppUtils::Params::getStrArg("extraLibs").
            split(DeployCore::getSeparator(0));

    auto listExtraPlugin = QuasarAppUtils::Params::getStrArg("extraPlugin").
            split(DeployCore::getSeparator(0));

    setExtraPath(listLibDir);
    setExtraNames(listNamesMasks);
    setExtraPlugins(listExtraPlugin);

    if (!initQmake()) {
        return false;
    }

    if (!initQmlInput()) {
        return false;
    }

    return true;
}

bool ConfigParser::parseInfoMode() {
    if ((QuasarAppUtils::Params::isEndable("v") ||
            QuasarAppUtils::Params::isEndable("version"))) {
        DeployCore::printVersion();
        return true;
    }

    DeployCore::help();

    return true;
}

bool ConfigParser::parseInitMode() {

    auto initLvl = QuasarAppUtils::Params::getStrArg("init");
    QString sourceUrl(":/Distro/Distributions/configures/Init.json");

    if (initLvl == "multi") {
        sourceUrl = ":/Distro/Distributions/configures/Init multiPackage configuration.json";
    } else if (initLvl == "single") {
        sourceUrl = ":/Distro/Distributions/configures/Init single configuration.json";
    }

    QFile configFile(DEFAULT_COFIGURATION_FILE);
    QFile source(sourceUrl);

    if (configFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {

        if (source.open(QIODevice::ReadOnly)) {
            configFile.write(source.readAll());
            source.close();
        }

        configFile.close();
    }

    return true;
}

bool ConfigParser::parseClearMode() {
    setTargetDir("./" + DISTRO_DIR);

    return true;
}

QSet<QString> ConfigParser::getQtPathesFromTargets() {
    QSet<QString> res;

    for (const auto &i: _config.targets()) {
        if (i.isValid() && !i.getQtPath().isEmpty()) {
            res.insert(i.getQtPath());
        }
    }

    return res;
}

bool ConfigParser::isNeededQt() const {
    for (const auto &i: _config.targets()) {
        if (i.isValid() && i.isDependetOfQt()) {
            return true;
        }
    }

    return false;
}

void ConfigParser::setTargetDir(const QString &target) {

    if (QuasarAppUtils::Params::isEndable("targetDir")) {
        _config.setTargetDir(QFileInfo(QuasarAppUtils::Params::getStrArg("targetDir")).absoluteFilePath());
    } else if (target.size()) {
        _config.setTargetDir(QFileInfo(target).absoluteFilePath());
    } else {

        _config.setTargetDir(QFileInfo("./" + DISTRO_DIR).absoluteFilePath());
        QuasarAppUtils::Params::log("flag targetDir not  used. use default target dir :" + _config.getTargetDir(),
                                           QuasarAppUtils::Info);
    }
}

bool ConfigParser::setTargets(const QStringList &value) {

    bool isfillList = false;

    for (const auto &i : value) {
        QFileInfo targetInfo(i);

        if (i.isEmpty())
            continue;

        if (targetInfo.isFile()) {

            _config.targetsEdit().unite(createTarget(QDir::fromNativeSeparators(i)));

            isfillList = true;
        }
        else if (targetInfo.isDir()) {
            if (!setBinDir(i)) {
                QuasarAppUtils::Params::log(i + " du not contains executable binaries!",
                                             QuasarAppUtils::Debug);
                continue;
            }
            isfillList = true;

        } else {
            QuasarAppUtils::Params::log(targetInfo.absoluteFilePath() + " not exits!",
                                         QuasarAppUtils::Debug);
        }
    }

    if (!isfillList)
        return false;

    setTargetDir();

    return true;
}

bool ConfigParser::setTargetsRecursive(const QString &dir) {
    if (!setBinDir(dir, true)) {
        QuasarAppUtils::Params::log("setBinDir failed!",
                                     QuasarAppUtils::Warning);
        return false;
    }

    setTargetDir();

    return true;
}

bool ConfigParser::setBinDir(const QString &dir, bool recursive) {
    QDir d(dir);
    if (dir.isEmpty() || !d.exists()) {
        QuasarAppUtils::Params::log(dir + " dir not exits!",
                                     QuasarAppUtils::Debug);
        return false;
    }
    QuasarAppUtils::Params::log("setBinDir check path: " + dir,
                                 QuasarAppUtils::Debug);
    QFileInfoList list;

    if (recursive) {
        list = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    } else {
        list = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    }

    bool result = false;
    for (const auto &file : list) {

        if (file.isDir()) {
            result |= setBinDir(file.absoluteFilePath(), recursive);
            continue;
        }

        auto name = file.fileName();
        auto sufix = file.completeSuffix();

        if (sufix.isEmpty() ||  name.contains(".dll", Qt::CaseInsensitive) ||
              name.contains(".so", Qt::CaseInsensitive) || name.contains(".exe", Qt::CaseInsensitive)) {

            result = true;

            _config.targetsEdit().unite(createTarget(QDir::fromNativeSeparators(file.absoluteFilePath())));

        }

       }

    return result;
}

QHash<QString, TargetInfo> ConfigParser::createTarget(const QString &target) {
    TargetInfo libinfo;
    auto key = target;
    if (_scaner->fillLibInfo(libinfo, key)) {
        return {{libinfo.fullPath(), libinfo}};
    }
    return {{key, {}}};
}

QHash<QString, TargetInfo>
ConfigParser::moveTarget(TargetInfo target, const QString& newLocation) {
    target.setPath(QFileInfo(newLocation).absolutePath());

    return {{newLocation, target}};

}

void ConfigParser::initIgnoreList()
{
    if (QuasarAppUtils::Params::isEndable("ignore")) {
        auto list = QuasarAppUtils::Params::getStrArg("ignore").
                split(DeployCore::getSeparator(0));

        for (const auto &i : list) {
            _config.ignoreList.addRule(IgnoreData(i));
        }

    }

    IgnoreData ruleUnix, ruleWin;
    Envirement envUnix, envWin;

    if (!QuasarAppUtils::Params::isEndable("deploySystem-with-libc")) {

        envUnix.addEnv(Envirement::recursiveInvairement("/lib", 3));
        envUnix.addEnv(Envirement::recursiveInvairement("/usr/lib", 3));
        ruleUnix.prority = SystemLib;
        ruleUnix.platform = Unix;
        ruleUnix.enfirement = envUnix;


        auto addRuleUnix = [&ruleUnix](const QString & lib) {
            ruleUnix.label = lib;
            return ruleUnix;
        };

        _config.ignoreList.addRule(addRuleUnix("libc"));
        _config.ignoreList.addRule(addRuleUnix("ld-"));
        _config.ignoreList.addRule(addRuleUnix("libpthread"));
        _config.ignoreList.addRule(addRuleUnix("libm"));
        _config.ignoreList.addRule(addRuleUnix("libz"));
        _config.ignoreList.addRule(addRuleUnix("librt"));
        _config.ignoreList.addRule(addRuleUnix("libnsl"));
        _config.ignoreList.addRule(addRuleUnix("libdl"));
        _config.ignoreList.addRule(addRuleUnix("libutil"));
        _config.ignoreList.addRule(addRuleUnix("libresolv"));
        _config.ignoreList.addRule(addRuleUnix("libBrokenLocale"));
        _config.ignoreList.addRule(addRuleUnix("libBrokenLocale"));
        _config.ignoreList.addRule(addRuleUnix("libSegFault"));
        _config.ignoreList.addRule(addRuleUnix("libanl"));
        _config.ignoreList.addRule(addRuleUnix("libcrypt"));
        _config.ignoreList.addRule(addRuleUnix("/gconv/"));
        _config.ignoreList.addRule(addRuleUnix("libnss"));
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    auto path = env.value("PATH");
    auto winPath = findWindowsPath(path);

    envWin.addEnv(Envirement::recursiveInvairement(winPath + "/System32", 2));
    envWin.addEnv(Envirement::recursiveInvairement(winPath + "/SysWOW64", 2));

    ruleWin.prority = SystemLib;
    ruleWin.platform = Win;
    ruleWin.enfirement = envWin;

    auto addRuleWin = [&ruleWin](const QString & lib) {
        ruleWin.label = lib;
        return ruleWin;
    };

    // win and core libs :  see https://en.wikipedia.org/wiki/Microsoft_Windows_library_files
    _config.ignoreList.addRule(addRuleWin("Hal.DLL"));
    _config.ignoreList.addRule(addRuleWin("NTDLL.DLL"));
    _config.ignoreList.addRule(addRuleWin("KERNEL32.DLL"));
    _config.ignoreList.addRule(addRuleWin("GDI32.DLL"));
    _config.ignoreList.addRule(addRuleWin("USER32.DLL"));
    _config.ignoreList.addRule(addRuleWin("COMCTL32.DLL"));
    _config.ignoreList.addRule(addRuleWin("COMDLG32.DLL"));
    _config.ignoreList.addRule(addRuleWin("WS2_32.DLL"));
    _config.ignoreList.addRule(addRuleWin("ADVAPI32.DLL"));
    _config.ignoreList.addRule(addRuleWin("NETAPI32.DLL"));
    _config.ignoreList.addRule(addRuleWin("OLE32.DLL"));
    _config.ignoreList.addRule(addRuleWin("SHSCRAP.DLL"));
    _config.ignoreList.addRule(addRuleWin("WINMM.DLL"));
    _config.ignoreList.addRule(addRuleWin("IMM32.DLL"));
    _config.ignoreList.addRule(addRuleWin("KernelBase.DLL"));
    _config.ignoreList.addRule(addRuleWin("dwmapi.DLL"));


}

void ConfigParser::initIgnoreEnvList() {
    QStringList ignoreEnvList;

    if (QuasarAppUtils::Params::isEndable("ignoreEnv")) {
        auto ignoreList = QuasarAppUtils::Params::getStrArg("ignoreEnv").
                split(DeployCore::getSeparator(0));


        for (const auto &i : ignoreList) {
            auto path = QFileInfo(i).absoluteFilePath();

            if (path.right(1) == "/" || path.right(1) == "\\") {
                path.remove(path.size() - 1, 1);
            }

            ignoreEnvList.append(path);
        }
    }

    ignoreEnvList.push_back(_config.appDir);
    ignoreEnvList.push_back(_config.getTargetDir());

    if (QuasarAppUtils::Params::isEndable("noRecursiveiIgnoreEnv")) {
        _config.envirement.setIgnoreEnvList(ignoreEnvList);
    } else {
        _config.envirement.setIgnoreEnvListRecursive(ignoreEnvList, _config.depchLimit);
    }


}

QString ConfigParser::getPathFrmoQmakeLine(const QString &in) const {
    auto list = in.split(':');
    if (list.size() > 1) {
        list.removeAt(0);
        return QFileInfo(list.join(':')).absoluteFilePath().remove('\r');
    }

    return "";
}

bool ConfigParser::initQmakePrivate(const QString &qmake) {
    QFileInfo info(qmake);

    QString basePath = info.absolutePath();
    if (!setQmake(qmake)) {
        QDir dir(basePath);

        if (!dir.cdUp()) {
            QuasarAppUtils::Params::log("fail init qmake",
                                               QuasarAppUtils::Error);
            return false;
        }

        QuasarAppUtils::Params::log("exec qmake fail!, try init qtDir from path:" + dir.absolutePath(),
                                           QuasarAppUtils::Warning);

        if (!setQtDir(dir.absolutePath())){
            QuasarAppUtils::Params::log("fail ini qmake",
                                               QuasarAppUtils::Error);
            return false;
        }

    }

    return true;
}

bool ConfigParser::initQmake() {

    auto qmake = QuasarAppUtils::Params::getStrArg("qmake");

    QFileInfo info(qmake);

    if (!info.isFile() || (info.baseName() != "qmake")) {

        auto qtList = getQtPathesFromTargets();

        if (qtList.isEmpty()) {

            if (!QuasarAppUtils::Params::isEndable("noCheckPATH") && isNeededQt()) {
                auto env = QProcessEnvironment::systemEnvironment();
                auto proc = DeployCore::findProcess(env.value("PATH"), "qmake");
                if (proc.isEmpty()) {
                    QuasarAppUtils::Params::log("The deployment target requir Qt libs, but init qmake is failed.",
                                                       QuasarAppUtils::Error);
                    return false;
                }

                return initQmakePrivate(proc);
            }
            QuasarAppUtils::Params::log("deploy only C libs because qmake is not found",
                                               QuasarAppUtils::Info);
            return true;

        }

        if (qtList.size() > 1) {
            QuasarAppUtils::Params::log("Your deployment targets were compiled by different qmake,"
                                               "qt auto-capture is not possible. Use the -qmake flag to solve this problem.",
                                               QuasarAppUtils::Error);
            return false;
        }

        auto qt = *qtList.begin();

        if (qt.right(3).compare("lib", Qt::CaseInsensitive)) {
            return initQmakePrivate(QFileInfo(qt + "/../bin/qmake").absoluteFilePath());
        }

        return initQmakePrivate(QFileInfo(qt + "/qmake").absoluteFilePath());
    }
    return initQmakePrivate(qmake);
}

bool ConfigParser::setQmake(const QString &value) {

    auto qmakeInfo = QFileInfo(QDir::fromNativeSeparators(value));

    if (!(qmakeInfo.fileName().compare("qmake", Qt::CaseInsensitive) ||
        qmakeInfo.fileName().compare("qmake.exe", Qt::CaseInsensitive))) {

        return false;
    }

    QProcess proc;
    proc.setProgram(qmakeInfo.absoluteFilePath());
    proc.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    proc.setArguments({"-query"});

    proc.start();
    if (!proc.waitForFinished(1000)) {
        QuasarAppUtils::Params::log("run qmake fail!");

        return false;
    }

    QString qmakeData = proc.readAll();
    auto list = qmakeData.split('\n');

    for (const auto &value : list) {
        if (value.contains("QT_INSTALL_LIBS")) {
            _config.qtDir.setLibs(getPathFrmoQmakeLine(value));
        } else if (value.contains("QT_INSTALL_LIBEXECS")) {
            _config.qtDir.setLibexecs(getPathFrmoQmakeLine(value));
        } else if (value.contains("QT_INSTALL_BINS")) {
            _config.qtDir.setBins(getPathFrmoQmakeLine(value));
        } else if (value.contains("QT_INSTALL_PLUGINS")) {
            _config.qtDir.setPlugins(getPathFrmoQmakeLine(value));
        } else if (value.contains("QT_INSTALL_QML")) {
            _config.qtDir.setQmls(getPathFrmoQmakeLine(value));
        } else if (value.contains("QT_INSTALL_TRANSLATIONS")) {
            _config.qtDir.setTranslations(getPathFrmoQmakeLine(value));
        } else if (value.contains("QT_INSTALL_DATA")) {
            _config.qtDir.setResources(getPathFrmoQmakeLine(value) + "/resources");
        } else if (value.contains("QMAKE_XSPEC")) {
            auto val = value.split(':').value(1);

            if (val.contains("win32")) {
                _config.qtDir.setQtPlatform(Platform::Win);
            } else {
                _config.qtDir.setQtPlatform(Platform::Unix);
            }
        }
    }
    _config.envirement.addEnv(_config.qtDir.getLibs());
    _config.envirement.addEnv(_config.qtDir.getBins());

    return true;
}

bool ConfigParser::setQtDir(const QString &value) {

    QFileInfo info(value);

    if (!QFile::exists(info.absoluteFilePath() + ("/bin"))) {
        QuasarAppUtils::Params::log("get qt bin fail!");
        return false;
    }
    _config.qtDir.setBins(info.absoluteFilePath() + ("/bin"));

    if (!QFile::exists(info.absoluteFilePath() + ("/lib"))) {
        QuasarAppUtils::Params::log("get qt lib fail!");
        return false;
    }
    _config.qtDir.setLibs(info.absoluteFilePath() + ("/lib"));

    if (!QFile::exists(info.absoluteFilePath() + ("/qml"))) {
        QuasarAppUtils::Params::log("get qt qml fail!");
    } else {
        _config.qtDir.setQmls(info.absoluteFilePath() + ("/qml"));
    }

    if (!QFile::exists(info.absoluteFilePath() + ("/plugins"))) {
        QuasarAppUtils::Params::log("get qt plugins fail!");
    } else {
        _config.qtDir.setPlugins(info.absoluteFilePath() + ("/plugins"));
    }

#ifdef Q_OS_UNIX
    if (!QFile::exists(info.absoluteFilePath() + ("/libexec"))) {
        QuasarAppUtils::Params::log("get qt libexec fail!");
    } else {
        _config.qtDir.setLibexecs(info.absoluteFilePath() + ("/libexec"));
    }
#endif
#ifdef Q_OS_WIN
    _config.qtDir.setLibexecs(info.absoluteFilePath() + ("/bin"));
#endif

    if (!QFile::exists(info.absoluteFilePath() + ("/translations"))) {
        QuasarAppUtils::Params::log("get qt translations fail!");
    } else {
        _config.qtDir.setTranslations(info.absoluteFilePath() + ("/translations"));
    }

    if (!QFile::exists(info.absoluteFilePath() + ("/resources"))) {
        QuasarAppUtils::Params::log("get qt resources fail!");
    } else {
        _config.qtDir.setResources(info.absoluteFilePath() + ("/resources"));
    }

#ifdef Q_OS_UNIX
    _config.qtDir.setQtPlatform(Platform::Unix);
#endif
#ifdef Q_OS_WIN
    _config.qtDir.setQtPlatform(Platform::Win);
#endif

    _config.envirement.addEnv(_config.qtDir.getLibs());
    _config.envirement.addEnv(_config.qtDir.getBins());

    return true;
}

void ConfigParser::setExtraPath(const QStringList &value) {
    QDir dir;

    for (const auto &i : value) {
        QFileInfo info(i);
        if (info.isDir()) {
            if (_config.targets().contains(info.absoluteFilePath())) {
                QuasarAppUtils::Params::log("skip the extra lib path because it is target!",
                                                   QuasarAppUtils::Info);
                continue;
            }

            dir.setPath(info.absoluteFilePath());
            auto extraDirs = getSetDirsRecursive(QDir::fromNativeSeparators(info.absoluteFilePath()), _config.depchLimit);
            _config.extraPaths.addExtraPaths(extraDirs);

            _config.envirement.addEnv(Envirement::recursiveInvairement(dir, 0, _config.depchLimit));
        } else if (i.size() > 1) {

            _config.extraPaths.addExtraPathsMasks({i});

            QuasarAppUtils::Params::log(i + " added like a path mask",
                                               QuasarAppUtils::Info);
        } else {
            QuasarAppUtils::Params::log(i + " not added in path mask because"
                                                   " the path mask must be large 2 characters",
                                               QuasarAppUtils::Warning);
        }
    }
}

void ConfigParser::setExtraNames(const QStringList &value) {
    for (const auto &i : value) {
        if (i.size() > 1) {
            _config.extraPaths.addtExtraNamesMasks({i});

            QuasarAppUtils::Params::log(i + " added like a file name mask",
                                               QuasarAppUtils::Info);
        } else {
            QuasarAppUtils::Params::log(i + " not added in file mask because"
                                                   " the file mask must be large 2 characters",
                                               QuasarAppUtils::Warning);
        }

    }
}

void ConfigParser::setExtraPlugins(const QStringList &value) {
    for (const auto &i : value) {
        if (!i.isEmpty())
            _config.extraPlugins.append(i);
    }
}

QString ConfigParser::findWindowsPath(const QString& path) const {
    auto list = path.split(';');
    QString win_magic = "windows";

    for (const auto &i: list ) {
        int index = i.indexOf(win_magic, 0, Qt::CaseInsensitive);
        if (index > 0 && i.size() == index + win_magic.size()) {
            return QDir::fromNativeSeparators(i);
        }
    }

    return "C:/" + win_magic;
}

iDistribution *ConfigParser::getDistribution() {
    if (QuasarAppUtils::Params::isEndable("qif")) {
        return new QIF(_fileManager);
    }

    return new DefaultDistro(_fileManager);
}

void ConfigParser::initEnvirement() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    auto path = env.value("PATH");

    _config.envirement.addEnv(env.value("LD_LIBRARY_PATH"));
    _config.envirement.addEnv(path);

    QStringList dirs;
#ifdef Q_OS_LINUX

    dirs.append(getDirsRecursive("/lib", 5));
    dirs.append(getDirsRecursive("/usr/lib", 5));
#else
    auto winPath = findWindowsPath(path);
    dirs.append(getDirsRecursive(winPath + "/System32", 2));
    dirs.append(getDirsRecursive(winPath + "/SysWOW64", 2));

#endif

    _config.envirement.addEnv(dirs);

    if (_config.envirement.size() < 2) {
        QuasarAppUtils::Params::log("system environment is empty",
                                           QuasarAppUtils::Warning);
    }
}

QStringList ConfigParser::getDirsRecursive(const QString &path, int maxDepch, int depch) {
    return getSetDirsRecursive(path, maxDepch, depch).values();
}

QSet<QString> ConfigParser::getSetDirsRecursive(const QString &path, int maxDepch, int depch) {
    QDir dir(path);

    QSet<QString> res = {dir.path()};

    if (maxDepch >= 0 && maxDepch <= depch) {
        return res;
    }

    auto list = dir.entryInfoList(QDir::Dirs| QDir::NoDotAndDotDot);

    for (const auto &subDir: list) {
        res.insert(subDir.absoluteFilePath());
        res.unite(getSetDirsRecursive(subDir.absoluteFilePath(), maxDepch, depch + 1));
    }

    return res;
}

bool ConfigParser::smartMoveTargets() {

    QHash<QString, TargetInfo> temp;
    bool result = true;
    for (auto i = _config.targets().cbegin(); i != _config.targets().cend(); ++i) {

        QFileInfo target(i.key());

        QString targetPath = _config.getTargetDir() + "/" + i.value().getPackage();

        if (DeployCore::isLib(target)) {
            targetPath += _config.getDistro(i.key()).getLibOutDir();
        } else {
            targetPath += _config.getDistro(i.key()).getBinOutDir();
        }

        if (!_fileManager->smartCopyFile(target.absoluteFilePath(), targetPath)) {
            result = false;
        }

        auto newTargetKey = targetPath + "/" + target.fileName();
        temp.unite(moveTarget(i.value(), newTargetKey));

        _config.packagesEdit()[i.value().getPackage()].addTarget(newTargetKey);

    }

    _config.targetsEdit() = temp;

    return result;
}

ConfigParser::ConfigParser(FileManager *filemanager, DependenciesScanner* scaner, Packing *pac):
    _fileManager(filemanager),
    _scaner(scaner),
    _packing(pac) {

    assert(_fileManager);
    assert(_scaner);
    assert(_packing);

#ifdef Q_OS_LINUX
    _config.appDir = QuasarAppUtils::Params::getStrArg("appPath");

    if (_config.appDir.right(4) == "/bin") {
        _config.appDir = _config.appDir.left(_config.appDir.size() - 4);
    }
#else
    _config.appDir = QuasarAppUtils::Params::getStrArg("appPath");
#endif

    QuasarAppUtils::Params::log("appDir = " + _config.appDir);
}
