/**************************************************************************
**
** This file is part of Installer Framework
**
** Copyright (c) 2011-2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/
#include <common/errors.h>
#include <common/fileutils.h>
#include <common/repositorygen.h>
#include <common/utils.h>
#include <settings.h>
#include <init.h>
#include <lib7z_facade.h>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

#include <iostream>

using namespace Lib7z;
using namespace QInstaller;

static void printUsage()
{
    const QString appName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    std::cout << "Usage: " << appName << " [options] repository-dir package1 [package2 ...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;

    printRepositoryGenOptions();

    std::cout << "  -u|--updateurl            url instructs clients to receive updates from a " << std::endl;
    std::cout << "                            different location" << std::endl;

    std::cout << "  --single                  Put only the given components (not their dependencies) " << std::endl;
    std::cout << "                            into the (already existing) repository" << std::endl;

    std::cout << "  -v|--verbose              Verbose output" << std::endl;

    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << appName << " -p ../examples/packages -c ../examples/config -u "
        "http://www.some-server.com:8080 repository/ com.nokia.sdk" << std::endl;
}

static int printErrorAndUsageAndExit(const QString &err)
{
    std::cerr << qPrintable(err) << std::endl << std::endl;
    printUsage();
    return 1;
}

static QString makeAbsolute(const QString &path)
{
    QFileInfo fi(path);
    if (fi.isAbsolute())
        return path;
    return QDir::current().absoluteFilePath(path);
}

int main(int argc, char** argv)
{
    try {
        QCoreApplication app(argc, argv);

        QInstaller::init();

        QStringList args = app.arguments().mid(1);

        QStringList excludedPackages;
        bool replaceSingleComponent = false;
        QString packagesDir;
        QString configDir;
        QString redirectUpdateUrl;

        //TODO: use a for loop without removing values from args like it is in binarycreator.cpp
        //for (QStringList::const_iterator it = args.begin(); it != args.end(); ++it) {
        while (!args.isEmpty() && args.first().startsWith(QLatin1Char('-'))) {
            if (args.first() == QLatin1String("--verbose") || args.first() == QLatin1String("-v")) {
                args.removeFirst();
                setVerbose(true);
            } else if (args.first() == QLatin1String("--exclude") || args.first() == QLatin1String("-e")) {
                args.removeFirst();
                if (args.isEmpty() || args.first().startsWith(QLatin1Char('-')))
                    return printErrorAndUsageAndExit(QObject::tr("Error: Package to exclude missing"));
                excludedPackages = args.first().split(QLatin1Char(','));
                args.removeFirst();
            } else if (args.first() == QLatin1String("--single")) {
                args.removeFirst();
                replaceSingleComponent = true;
            } else if (args.first() == QLatin1String("-p") || args.first() == QLatin1String("--packages")) {
                args.removeFirst();
                if (args.isEmpty()) {
                    return printErrorAndUsageAndExit(QObject::tr("Error: Packages parameter missing "
                        "argument"));
                }
                if (!QFileInfo(args.first()).exists()) {
                    return printErrorAndUsageAndExit(QObject::tr("Error: Package directory not found "
                        "at the specified location"));
                }
                packagesDir = args.first();
                args.removeFirst();
            } else if (args.first() == QLatin1String("-c") || args.first() == QLatin1String("--config")) {
                args.removeFirst();
                if (args.isEmpty())
                    return printErrorAndUsageAndExit(QObject::tr("Error: Config parameter missing argument"));
                const QFileInfo fi(args.first());
                if (!fi.exists()) {
                    return printErrorAndUsageAndExit(QObject::tr("Error: Config directory %1 not found "
                        "at the specified location").arg(args.first()));
                }
                if (!fi.isDir()) {
                    return printErrorAndUsageAndExit(QObject::tr("Error: Configuration %1 is not a "
                        "directory").arg(args.first()));
                }
                if (!fi.isReadable()) {
                    return printErrorAndUsageAndExit(QObject::tr("Error: Config directory %1 is not "
                        "readable").arg(args.first()));
                }
                configDir = args.first();
                args.removeFirst();
            } else if (args.first() == QLatin1String("-u") || args.first() == QLatin1String("--updateurl")) {
                args.removeFirst();
                if (args.isEmpty())
                    return printErrorAndUsageAndExit(QObject::tr("Error: Config parameter missing argument"));
                redirectUpdateUrl = args.first();
                args.removeFirst();
            }
            else {
                printUsage();
                return 1;
            }
        }

        //TODO: adjust to the new argument/option usage
        if ((packagesDir.isEmpty() && configDir.isEmpty() && args.count() < 4)
            || ((packagesDir.isEmpty() || configDir.isEmpty()) && args.count() < 3) //use the old check
            || (args.count() < 2)) {    //only one dir set by the new options
                // both dirs set by the new options
                printUsage();
                return 1;
        }

        int argsPosition = 0;
        bool needPrintUsage = false;
        if (packagesDir.isEmpty()) {
            std::cout << "!!! A stand alone package directory argument is deprecated. Please use the pre "
                "argument." << std::endl;
            needPrintUsage |= true;
            packagesDir = makeAbsolute(args[argsPosition++]);
        }

        if (configDir.isEmpty()) {
            std::cout << "!!! A stand alone config directory argument is deprecated. Please use the pre "
                "argument." << std::endl;
            needPrintUsage |= true;
            configDir = makeAbsolute(args[argsPosition++]);
        }
        if (needPrintUsage) {
            printUsage();
        }

        const QString repositoryDir = makeAbsolute(args[argsPosition++]);
        const QStringList components = args.mid(argsPosition);

        if (!replaceSingleComponent && QFile::exists(repositoryDir)) {
            throw QInstaller::Error(QObject::tr("Repository target folder %1 already exists!")
                .arg(repositoryDir));
        }

        PackageInfoVector packages = createListOfPackages(components, packagesDir, excludedPackages,
            !replaceSingleComponent);
        QMap<QString, QString> pathToVersionMapping = buildPathToVersionMap(packages);

        for (PackageInfoVector::const_iterator it = packages.begin(); it != packages.end(); ++it) {
            const QFileInfo fi(repositoryDir, it->name);
            if (fi.exists())
                removeDirectory(fi.absoluteFilePath());
        }

        copyComponentData(packagesDir, repositoryDir, packages);

        TempDirDeleter tmpDeleter;
        const QString metaTmp = createTemporaryDirectory();
        tmpDeleter.add(metaTmp);

        const Settings &settings = Settings::fromFileAndPrefix(configDir + QLatin1String("/config.xml"),
            configDir);
        generateMetaDataDirectory(metaTmp, repositoryDir, packages, settings.applicationName(),
            settings.applicationVersion(), redirectUpdateUrl);
        compressMetaDirectories(metaTmp, metaTmp, pathToVersionMapping);

        QFile::remove(QFileInfo(repositoryDir, QLatin1String("Updates.xml")).absoluteFilePath());
        moveDirectoryContents(metaTmp, repositoryDir);
        return 0;
    } catch (const Lib7z::SevenZipException &e) {
        std::cerr << e.message() << std::endl;
    } catch (const QInstaller::Error &e) {
        std::cerr << e.message() << std::endl;
    }
    return 1;
}
