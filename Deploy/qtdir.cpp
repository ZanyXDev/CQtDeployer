#include "pathutils.h"
#include "qtdir.h"

QString QtDir::getLibs() const {
    return libs;
}

void QtDir::setLibs(const QString &value) {
    libs = PathUtils::fixPath(value);
}

QString QtDir::getBins() const {
    return bins;
}

void QtDir::setBins(const QString &value) {
    bins =  PathUtils::fixPath(value);
}

QString QtDir::getLibexecs() const {
    return libexecs;
}

void QtDir::setLibexecs(const QString &value) {
    libexecs =  PathUtils::fixPath(value);
}

QString QtDir::getPlugins() const {
    return plugins;
}

void QtDir::setPlugins(const QString &value) {
    plugins =  PathUtils::fixPath(value);
}

QString QtDir::getQmls() const {
    return qmls;
}

void QtDir::setQmls(const QString &value)
{
    qmls =  PathUtils::fixPath(value);
}

QString QtDir::getTranslations() const {
    return translations;
}

void QtDir::setTranslations(const QString &value) {
    translations =  PathUtils::fixPath(value);
}

QString QtDir::getResources() const{
    return resources;
}

void QtDir::setResources(const QString &value) {
    resources =  PathUtils::fixPath(value);
}

Platform QtDir::getQtPlatform() const {
    return qtPlatform;
}

void QtDir::setQtPlatform(const Platform &value) {
    qtPlatform = value;
}

bool QtDir::isQt(QString path) const {

    path =  PathUtils::fixPath(path);

    return
            (!libs.isEmpty() && path.contains(libs)) ||
            (!bins.isEmpty() && path.contains(bins)) ||
    (!libexecs.isEmpty() && path.contains(libexecs)) ||
    (!plugins.isEmpty() && path.contains(plugins)) ||
    (!qmls.isEmpty() && path.contains(qmls)) ||
    (!translations.isEmpty() && path.contains(translations)) ||
    (!resources.isEmpty() && path.contains(resources));
}
