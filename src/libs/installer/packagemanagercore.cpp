/**************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Installer Framework.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
**************************************************************************/
#include "packagemanagercore.h"
#include "packagemanagercore_p.h"

#include "adminauthorization.h"
#include "binarycontent.h"
#include "component.h"
#include "componentmodel.h"
#include "downloadarchivesjob.h"
#include "errors.h"
#include "globals.h"
#include "messageboxhandler.h"
#include "packagemanagerproxyfactory.h"
#include "progresscoordinator.h"
#include "qprocesswrapper.h"
#include "qsettingswrapper.h"
#include "remoteclient.h"
#include "remotefileengine.h"
#include "settings.h"
#include "installercalculator.h"
#include "uninstallercalculator.h"
#include "loggingutils.h"
#include "lib7zarchive.h"

#include <productkeycheck.h>

#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrentRun>

#include <QtCore/QMutex>
#include <QtCore/QRegExp>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryFile>
#include <QtCore/QTextCodec>
#include <QtCore/QTextDecoder>
#include <QtCore/QTextEncoder>
#include <QtCore/QTextStream>

#include <QDesktopServices>
#include <QFileDialog>
#include <QRegularExpression>

#include "sysinfo.h"
#include "updateoperationfactory.h"

#ifdef Q_OS_WIN
#include "qt_windows.h"
#include <limits>
#endif

#include <QStandardPaths>

/*!
    \namespace QInstaller
    \inmodule QtInstallerFramework

    \keyword qinstaller-module

    \brief Contains classes to implement the core functionality of the Qt
    Installer Framework and the installer UI.
*/

using namespace QInstaller;

/*!
    \class QInstaller::PackageManagerCore
    \inmodule QtInstallerFramework
    \brief The PackageManagerCore class provides the core functionality of the Qt Installer
        Framework.
*/

/*!
    \enum PackageManagerCore::WizardPage

    This enum type holds the pre-defined package manager pages:

    \value  Introduction
            \l{Introduction Page}
    \value  TargetDirectory
            \l{Target Directory Page}
    \value  ComponentSelection
            \l{Component Selection Page}
    \value  LicenseCheck
            \l{License Agreement Page}
    \value  StartMenuSelection
            \l{Start Menu Directory Page}
    \value  ReadyForInstallation
            \l{Ready for Installation Page}
    \value  PerformInstallation
            \l{Perform Installation Page}
    \value  InstallationFinished
            \l{Finished Page}

    \omitvalue End
*/

/*!
    \enum PackageManagerCore::Status

    This enum type holds the package manager status:

    \value  Success
            Installation was successful.
    \value  Failure
            Installation failed.
    \value  Running
            Installation is in progress.
    \value  Canceled
            Installation was canceled.
    \value  Unfinished
            Installation was not completed.
    \value  ForceUpdate
            Installation has to be updated.
    \value  EssentialUpdated
            Installation essential components were updated.
*/

/*!
    \property PackageManagerCore::status
    \brief Installation status.
*/

/*!
    \fn QInstaller::PackageManagerCore::aboutCalculateComponentsToInstall() const

    \sa {installer::aboutCalculateComponentsToInstall}{installer.aboutCalculateComponentsToInstall}
*/

/*!
    \fn QInstaller::PackageManagerCore::finishedCalculateComponentsToInstall() const

    \sa {installer::finishedCalculateComponentsToInstall}{installer.finishedCalculateComponentsToInstall}
*/

/*!
    \fn QInstaller::PackageManagerCore::aboutCalculateComponentsToUninstall() const

    \sa {installer::aboutCalculateComponentsToUninstall}{installer.aboutCalculateComponentsToUninstall}
*/

/*!
    \fn QInstaller::PackageManagerCore::finishedCalculateComponentsToUninstall() const

    \sa {installer::finishedCalculateComponentsToUninstall}{installer.finishedCalculateComponentsToUninstall}
*/

/*!
    \fn QInstaller::PackageManagerCore::componentAdded(QInstaller::Component *comp)

    Emitted when the new root component \a comp is added.

    \sa {installer::componentAdded}{installer.componentAdded}
    \sa rootComponentsAdded(), updaterComponentsAdded()
*/

/*!
    \fn QInstaller::PackageManagerCore::rootComponentsAdded(QList<QInstaller::Component*> components)

    Emitted when the list of root components specified by \a components is added.

    \sa {installer::rootComponentsAdded}{installer.rootComponentsAdded}
    \sa componentAdded(), updaterComponentsAdded()
*/

/*!
    \fn QInstaller::PackageManagerCore::updaterComponentsAdded(QList<QInstaller::Component*> components)

    Emitted when a new list of updater components specified by \a components is added.

    \sa {installer::updaterComponentsAdded}{installer.updaterComponentsAdded}
    \sa componentAdded(), rootComponentsAdded()
*/

/*!
    \fn QInstaller::PackageManagerCore::valueChanged(const QString &key, const QString &value)

    Emitted when the value \a value of the key \a key changes.

    \sa {installer::valueChanged}{installer.valueChanged}, setValue()
*/

/*!
    \fn QInstaller::PackageManagerCore::currentPageChanged(int page)

    Emitted when the current page \a page changes.

    \sa {installer::currentPageChanged}{installer.currentPageChanged}
*/

/*!
    \fn QInstaller::PackageManagerCore::finishButtonClicked()

    \sa {installer::finishButtonClicked}{installer.finishButtonClicked}
*/

/*!
    \fn QInstaller::PackageManagerCore::metaJobProgress(int progress)

    Triggered with progress updates of the communication with a remote
    repository. Values of \a progress range from 0 to 100.

    \sa {installer::metaJobProgress}{installer.metaJobProgress}
*/

/*!
    \fn QInstaller::PackageManagerCore::metaJobTotalProgress(int progress)

    Triggered when the total \a progress value of the communication with a
    remote repository changes.

    \sa {installer::metaJobTotalProgress}{installer.metaJobTotalProgress}
*/

/*!
    \fn QInstaller::PackageManagerCore::metaJobInfoMessage(const QString &message)

    Triggered with informative updates, \a message, of the communication with a remote repository.

    \sa {installer::metaJobInfoMessage}{installer.metaJobInfoMessage}
*/

/*!
    \fn QInstaller::PackageManagerCore::startAllComponentsReset()

    \sa {installer::startAllComponentsReset}{installer.startAllComponentsReset}
    \sa finishAllComponentsReset()
*/

/*!
    \fn QInstaller::PackageManagerCore::finishAllComponentsReset(const QList<QInstaller::Component*> &rootComponents)

    Triggered when the list of new root components, \a rootComponents, has been updated.

    \sa {installer::finishAllComponentsReset}{installer.finishAllComponentsReset}
    \sa startAllComponentsReset()
*/

/*!
    \fn QInstaller::PackageManagerCore::startUpdaterComponentsReset()

    \sa {installer::startUpdaterComponentsReset}{installer.startUpdaterComponentsReset}
*/

/*!
    \fn QInstaller::PackageManagerCore::finishUpdaterComponentsReset(const QList<QInstaller::Component*> &componentsWithUpdates)

    Triggered when the list of available remote updates, \a componentsWithUpdates,
    has been updated.

    \sa {installer::finishUpdaterComponentsReset}{installer.finishUpdaterComponentsReset}
*/

/*!
    \fn QInstaller::PackageManagerCore::installationStarted()

    \sa {installer::installationStarted}{installer.installationStarted},
        installationFinished(), installationInterrupted()
*/

/*!
    \fn QInstaller::PackageManagerCore::installationInterrupted()

    \sa {installer::installationInterrupted}{installer.installationInterrupted},
        interrupt(), installationStarted(), installationFinished()
*/

/*!
    \fn QInstaller::PackageManagerCore::installationFinished()

    \sa {installer::installationFinished}{installer.installationFinished},
        installationStarted(), installationInterrupted()
*/

/*!
    \fn QInstaller::PackageManagerCore::updateFinished()

    \sa {installer::installationFinished}{installer.installationFinished}
*/

/*!
    \fn QInstaller::PackageManagerCore::uninstallationStarted()

    \sa {installer::uninstallationStarted}{installer.uninstallationStarted}
    \sa uninstallationFinished()
*/

/*!
    \fn QInstaller::PackageManagerCore::uninstallationFinished()

    \sa {installer::uninstallationFinished}{installer.uninstallationFinished}
    \sa uninstallationStarted()
*/

/*!
    \fn QInstaller::PackageManagerCore::offlineGenerationStarted()

    \sa {installer::offlineGenerationStarted()}{installer.offlineGenerationStarted()}
*/

/*!
    \fn QInstaller::PackageManagerCore::offlineGenerationFinished()

    \sa {installer::offlineGenerationFinished()}{installer.offlineGenerationFinished()}
*/

/*!
    \fn QInstaller::PackageManagerCore::titleMessageChanged(const QString &title)

    Emitted when the text of the installer status (on the PerformInstallation page) changes to
    \a title.

    \sa {installer::titleMessageChanged}{installer.titleMessageChanged}
*/

/*!
    \fn QInstaller::PackageManagerCore::wizardPageInsertionRequested(QWidget *widget, QInstaller::PackageManagerCore::WizardPage page)

    Emitted when a custom \a widget is about to be inserted into \a page by
    addWizardPage().

    \sa {installer::wizardPageInsertionRequested}{installer.wizardPageInsertionRequested}
*/

/*!
    \fn QInstaller::PackageManagerCore::wizardPageRemovalRequested(QWidget *widget)

    Emitted when a \a widget is removed by removeWizardPage().

    \sa {installer::wizardPageRemovalRequested}{installer.wizardPageRemovalRequested}
*/

/*!
    \fn QInstaller::PackageManagerCore::wizardWidgetInsertionRequested(QWidget *widget,
        QInstaller::PackageManagerCore::WizardPage page, int position)

    Emitted when a \a widget is inserted into \a page by addWizardPageItem(). If several widgets
    are added to the same \a page, widget with lower \a position number will be inserted on top.

    \sa {installer::wizardWidgetInsertionRequested}{installer.wizardWidgetInsertionRequested}
*/

/*!
    \fn QInstaller::PackageManagerCore::wizardWidgetRemovalRequested(QWidget *widget)

    Emitted when a \a widget is removed by removeWizardPageItem().

    \sa {installer::wizardWidgetRemovalRequested}{installer.wizardWidgetRemovalRequested}
*/

/*!
    \fn QInstaller::PackageManagerCore::wizardPageVisibilityChangeRequested(bool visible, int page)

    Emitted when the visibility of the page with the ID \a page changes to \a visible.

    \sa setDefaultPageVisible()
    \sa {installer::wizardPageVisibilityChangeRequested}{installer.wizardPageVisibilityChangeRequested}
*/

/*!
    \fn QInstaller::PackageManagerCore::setValidatorForCustomPageRequested(QInstaller::Component *component, const QString &name,
                                                               const QString &callbackName)

    Requests that a validator be set for the custom page specified by \a name and
    \a callbackName for the component \a component. Triggered when
    setValidatorForCustomPage() is called.

    \sa {installer::setValidatorForCustomPageRequested}{installer.setValidatorForCustomPageRequested}
*/

/*!
    \fn QInstaller::PackageManagerCore::setAutomatedPageSwitchEnabled(bool request)

    Triggered when the automatic switching from the perform installation to the installation
    finished page is enabled (\a request is \c true) or disabled (\a request is \c false).

    The automatic switching is disabled automatically when for example the user expands or unexpands
    the \uicontrol Details section of the PerformInstallation page.

    \sa {installer::setAutomatedPageSwitchEnabled}{installer.setAutomatedPageSwitchEnabled}
*/

/*!
    \fn QInstaller::PackageManagerCore::coreNetworkSettingsChanged()

    \sa {installer::coreNetworkSettingsChanged}{installer.coreNetworkSettingsChanged}
*/

/*!
    \fn QInstaller::PackageManagerCore::guiObjectChanged(QObject *gui)

    Emitted when the GUI object is set to \a gui.
*/

/*!
    \fn QInstaller::PackageManagerCore::unstableComponentFound(const QString &type, const QString &errorMessage, const QString &component)

    Emitted when an unstable \a component is found containing an unstable \a type and \a errorMessage.
*/

/*!
    \fn QInstaller::PackageManagerCore::installerBinaryMarkerChanged(qint64 magicMarker)

    Emitted when installer binary marker \a magicMarker has changed.
*/

/*!
    \fn QInstaller::PackageManagerCore::componentsRecalculated()

    Emitted when the component tree is recalculated. In a graphical interface,
    this signal is emitted also after the categories are fetched.
*/

Q_GLOBAL_STATIC(QMutex, globalModelMutex);
static QFont *sVirtualComponentsFont = nullptr;
Q_GLOBAL_STATIC(QMutex, globalVirtualComponentsFontMutex);

static bool sNoForceInstallation = false;
static bool sNoDefaultInstallation = false;
static bool sVirtualComponentsVisible = false;
static bool sCreateLocalRepositoryFromBinary = false;

static bool componentMatches(const Component *component, const QString &name,
    const QString &version = QString())
{
    if (name.isEmpty() || component->name() != name)
        return false;

    if (version.isEmpty())
        return true;

    // can be remote or local version
    return PackageManagerCore::versionMatches(component->value(scVersion), version);
}

/*!
    Creates the maintenance tool in the installation directory.
*/
void PackageManagerCore::writeMaintenanceTool()
{
    if (d->m_disableWriteMaintenanceTool) {
        qCDebug(QInstaller::lcInstallerInstallLog()) << "Maintenance tool writing disabled.";
        return;
    }

    if (d->m_needToWriteMaintenanceTool) {
        try {
            d->writeMaintenanceTool(d->m_performedOperationsOld + d->m_performedOperationsCurrentSession);

            bool gainedAdminRights = false;
            if (!directoryWritable(d->targetDir())) {
                gainAdminRights();
                gainedAdminRights = true;
            }
            d->m_localPackageHub->writeToDisk();
            if (gainedAdminRights)
                dropAdminRights();
            d->m_needToWriteMaintenanceTool = false;
        } catch (const Error &error) {
            qCritical() << "Error writing Maintenance Tool: " << error.message();
            MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(),
                QLatin1String("WriteError"), tr("Error writing Maintenance Tool"), error.message(),
                QMessageBox::Ok, QMessageBox::Ok);
        }
    }
}

/*!
    Creates the maintenance tool configuration files.
*/
void PackageManagerCore::writeMaintenanceConfigFiles()
{
    d->writeMaintenanceConfigFiles();
}

/*!
    Disables writing of maintenance tool for the current session if \a disable is \c true.
 */
void PackageManagerCore::disableWriteMaintenanceTool(bool disable)
{
    d->m_disableWriteMaintenanceTool = disable;
}

/*!
    Resets the class to its initial state.
*/
void PackageManagerCore::reset()
{
    d->m_completeUninstall = false;
    d->m_needsHardRestart = false;
    d->m_status = PackageManagerCore::Unfinished;
    d->m_installerBaseBinaryUnreplaced.clear();
    d->m_coreCheckedHash.clear();
    d->m_componentsToInstallCalculated = false;
}

/*!
    Sets the maintenance tool UI to \a gui.
*/
void PackageManagerCore::setGuiObject(QObject *gui)
{
    if (gui == d->m_guiObject)
        return;
    d->m_guiObject = gui;
    emit guiObjectChanged(gui);
}

/*!
    Returns the GUI object.
*/
QObject *PackageManagerCore::guiObject() const
{
    return d->m_guiObject;
}

/*!
    Sets the uninstallation to be \a complete. If \a complete is false, only components deselected
    by the user will be uninstalled. This option applies only on uninstallation.

    \sa {installer::setCompleteUninstallation}{installer.setCompleteUninstallation}
 */
void PackageManagerCore::setCompleteUninstallation(bool complete)
{
    d->m_completeUninstall = complete;
}

/*!
    \sa {installer::cancelMetaInfoJob}{installer.cancelMetaInfoJob}
 */
void PackageManagerCore::cancelMetaInfoJob()
{
    d->m_metadataJob.cancel();
}

/*!
    \sa {installer::componentsToInstallNeedsRecalculation}{installer.componentsToInstallNeedsRecalculation}
 */
void PackageManagerCore::componentsToInstallNeedsRecalculation()
{
    d->clearInstallerCalculator();
    d->clearUninstallerCalculator();
    QList<Component*> selectedComponentsToInstall = componentsMarkedForInstallation();

    d->m_componentsToInstallCalculated =
            d->installerCalculator()->appendComponentsToInstall(selectedComponentsToInstall);

    QList<Component *> componentsToInstall = d->installerCalculator()->orderedComponentsToInstall();

    QList<Component *> selectedComponentsToUninstall;
    foreach (Component *component, components(ComponentType::All)) {
        if (component->uninstallationRequested() && !selectedComponentsToInstall.contains(component))
            selectedComponentsToUninstall.append(component);
    }

    d->uninstallerCalculator()->appendComponentsToUninstall(selectedComponentsToUninstall);

    QSet<Component *> componentsToUninstall = d->uninstallerCalculator()->componentsToUninstall();

    foreach (Component *component, components(ComponentType::All))
        component->setInstallAction(component->isInstalled()
                           ? ComponentModelHelper::KeepInstalled
                           : ComponentModelHelper::KeepUninstalled);
    foreach (Component *component, componentsToUninstall)
        component->setInstallAction(ComponentModelHelper::Uninstall);
    foreach (Component *component, componentsToInstall)
        component->setInstallAction(ComponentModelHelper::Install);

    // update all nodes uncompressed size
    foreach (Component *const component, components(ComponentType::Root))
        component->updateUncompressedSize(); // this is a recursive call
}

/*!
    Forces a recalculation of components to install.
    \sa {installer::clearComponentsToInstallCalculated}{installer.clearComponentsToInstallCalculated}
 */
void PackageManagerCore::clearComponentsToInstallCalculated()
{
    d->m_componentsToInstallCalculated = false;
}

/*!
   \sa {installer::autoAcceptMessageBoxes}{installer.autoAcceptMessageBoxes}
   \sa autoRejectMessageBoxes(), setMessageBoxAutomaticAnswer()
 */
void PackageManagerCore::autoAcceptMessageBoxes()
{
    MessageBoxHandler::instance()->setDefaultAction(MessageBoxHandler::Accept);
}

/*!
   \sa {installer::autoRejectMessageBoxes}{installer.autoRejectMessageBoxes}
   \sa autoAcceptMessageBoxes(), setMessageBoxAutomaticAnswer()
 */
void PackageManagerCore::autoRejectMessageBoxes()
{
    MessageBoxHandler::instance()->setDefaultAction(MessageBoxHandler::Reject);
}

/*!
   Automatically closes the message box with the ID \a identifier as if the user had pressed
   \a button.

   This can be used for unattended (automatic) installations.

   \sa {installer::setMessageBoxAutomaticAnswer}{installer.setMessageBoxAutomaticAnswer}
   \sa QMessageBox, autoAcceptMessageBoxes(), autoRejectMessageBoxes()
 */
void PackageManagerCore::setMessageBoxAutomaticAnswer(const QString &identifier, int button)
{
    MessageBoxHandler::instance()->setAutomaticAnswer(identifier,
        static_cast<QMessageBox::Button>(button));
}

/*!
   Automatically uses the default button value set for the message box.

   This can be used for unattended (automatic) installations.
 */
void PackageManagerCore::acceptMessageBoxDefaultButton()
{
    MessageBoxHandler::instance()->setDefaultAction(MessageBoxHandler::Default);
}

/*!
    Automatically accepts all license agreements required to install the selected components.

    \sa {installer::setAutoAcceptLicenses}{installer.setAutoAcceptLicenses}
*/
void PackageManagerCore::setAutoAcceptLicenses()
{
    d->m_autoAcceptLicenses = true;
}

/*!
   Automatically sets the existing directory or filename \a value to QFileDialog with the ID
   \a identifier. QFileDialog can be called from script.

   This can be used for unattended (automatic) installations.

   \sa {installer::setFileDialogAutomaticAnswer}{installer.setFileDialogAutomaticAnswer}
   \sa {QFileDialog::getExistingDirectory}{QFileDialog.getExistingDirectory}
   \sa {QFileDialog::getOpenFileName}{QFileDialog.getOpenFileName}
 */
void PackageManagerCore::setFileDialogAutomaticAnswer(const QString &identifier, const QString &value)
{
    m_fileDialogAutomaticAnswers.insert(identifier, value);
}

/*!
   Removes the automatic answer from QFileDialog with the ID \a identifier.
   QFileDialog can be called from script.

   \sa {installer::removeFileDialogAutomaticAnswer}{installer.removeFileDialogAutomaticAnswer}
   \sa {QFileDialog::getExistingDirectory}{QFileDialog.getExistingDirectory}
   \sa {QFileDialog::getOpenFileName}{QFileDialog.getOpenFileName}
 */
void PackageManagerCore::removeFileDialogAutomaticAnswer(const QString &identifier)
{
    m_fileDialogAutomaticAnswers.remove(identifier);
}

/*!
   Returns \c true if QFileDialog  with the ID \a identifier has an automatic answer set.

   \sa {installer::containsFileDialogAutomaticAnswer}{installer.containsFileDialogAutomaticAnswer}
   \sa {installer::removeFileDialogAutomaticAnswer}{installer.removeFileDialogAutomaticAnswer}
   \sa {QFileDialog::getExistingDirectory}{QFileDialog.getExistingDirectory}
   \sa {QFileDialog::getOpenFileName}{QFileDialog.getOpenFileName}
 */
bool PackageManagerCore::containsFileDialogAutomaticAnswer(const QString &identifier) const
{
    return m_fileDialogAutomaticAnswers.contains(identifier);
}

/*!
 * Returns the hash of file dialog automatic answers
 * \sa setFileDialogAutomaticAnswer()
 */
QHash<QString, QString> PackageManagerCore::fileDialogAutomaticAnswers() const
{
    return m_fileDialogAutomaticAnswers;
}

/*!
    Set to automatically confirm install, update or remove without asking user.
*/
void PackageManagerCore::setAutoConfirmCommand()
{
    d->m_autoConfirmCommand = true;
}

/*!
    Returns the size of the component \a component as \a value.
*/
quint64 PackageManagerCore::size(QInstaller::Component *component, const QString &value) const
{
    if (component->installAction() == ComponentModelHelper::Install)
        return component->value(value).toLongLong();
    return quint64(0);
}

/*!
   \sa {installer::requiredDiskSpace}{installer.requiredDiskSpace}
   \sa requiredTemporaryDiskSpace()
 */
quint64 PackageManagerCore::requiredDiskSpace() const
{
    quint64 result = 0;

    foreach (QInstaller::Component *component, orderedComponentsToInstall())
        result += size(component, isOfflineGenerator() ? scCompressedSize : scUncompressedSize);

    return result;
}

/*!
   \sa {installer::requiredTemporaryDiskSpace}{installer.requiredTemporaryDiskSpace}
   \sa requiredDiskSpace()
 */
quint64 PackageManagerCore::requiredTemporaryDiskSpace() const
{
    if (isOfflineOnly())
        return 0;

    quint64 result = 0;
    foreach (QInstaller::Component *component, orderedComponentsToInstall())
        result += size(component, scCompressedSize);
    return result;
}

/*!
    Returns the number of archives that will be downloaded.

    \a partProgressSize is reserved for the download progress.
*/
int PackageManagerCore::downloadNeededArchives(double partProgressSize)
{
    Q_ASSERT(partProgressSize >= 0 && partProgressSize <= 1);

    QList<QPair<QString, QString> > archivesToDownload;
    quint64 archivesToDownloadTotalSize = 0;
    QList<Component*> neededComponents = orderedComponentsToInstall();
    foreach (Component *component, neededComponents) {
        // collect all archives to be downloaded
        const QStringList toDownload = component->downloadableArchives();
        foreach (const QString &versionFreeString, toDownload) {
            archivesToDownload.push_back(qMakePair(QString::fromLatin1("installer://%1/%2")
                .arg(component->name(), versionFreeString), QString::fromLatin1("%1/%2/%3")
                .arg(component->repositoryUrl().toString(), component->name(), versionFreeString)));
        }
        archivesToDownloadTotalSize += component->value(scCompressedSize).toULongLong();
    }

    if (archivesToDownload.isEmpty())
        return 0;

    ProgressCoordinator::instance()->emitLabelAndDetailTextChanged(tr("\nDownloading packages..."));

    DownloadArchivesJob archivesJob(this);
    archivesJob.setAutoDelete(false);
    archivesJob.setArchivesToDownload(archivesToDownload);
    archivesJob.setExpectedTotalSize(archivesToDownloadTotalSize);
    connect(this, &PackageManagerCore::installationInterrupted, &archivesJob, &Job::cancel);
    connect(&archivesJob, &DownloadArchivesJob::outputTextChanged,
            ProgressCoordinator::instance(), &ProgressCoordinator::emitLabelAndDetailTextChanged);
    connect(&archivesJob, &DownloadArchivesJob::downloadStatusChanged,
            ProgressCoordinator::instance(), &ProgressCoordinator::downloadStatusChanged);

    ProgressCoordinator::instance()->registerPartProgress(&archivesJob,
        SIGNAL(progressChanged(double)), partProgressSize);

    archivesJob.start();
    archivesJob.waitForFinished();

    if (archivesJob.error() == Job::Canceled)
        interrupt();
    else if (archivesJob.error() != Job::NoError)
        throw Error(archivesJob.errorString());

    if (d->statusCanceledOrFailed())
        throw Error(tr("Installation canceled by user."));

    ProgressCoordinator::instance()->emitDownloadStatus(tr("All downloads finished."));

    return archivesJob.numberOfDownloads();
}

/*!
    Returns \c true if an essential component update is found.
*/
bool PackageManagerCore::foundEssentialUpdate() const
{
    return d->m_foundEssentialUpdate;
}

/*!
    Sets the value of \a foundEssentialUpdate, defaults to \c true.
*/
void PackageManagerCore::setFoundEssentialUpdate(bool foundEssentialUpdate)
{
    d->m_foundEssentialUpdate = foundEssentialUpdate;
}

/*!
    Returns \c true if a hard restart of the application is requested.
*/
bool PackageManagerCore::needsHardRestart() const
{
    return d->m_needsHardRestart;
}

/*!
    Enables a component to request a hard restart of the application if
    \a needsHardRestart is set to \c true.
*/
void PackageManagerCore::setNeedsHardRestart(bool needsHardRestart)
{
    d->m_needsHardRestart = needsHardRestart;
}

/*!
    Cancels the installation and performs the UNDO step of all already executed
    operations.
*/
void PackageManagerCore::rollBackInstallation()
{
    emit titleMessageChanged(tr("Canceling the Installer"));

    // this unregisters all operation progressChanged connected
    ProgressCoordinator::instance()->setUndoMode();
    const int progressOperationCount = d->countProgressOperations(d->m_performedOperationsCurrentSession);
    const double progressOperationSize = double(1) / progressOperationCount;

    // reregister all the undo operations with the new size to the ProgressCoordinator
    foreach (Operation *const operation, d->m_performedOperationsCurrentSession) {
        QObject *const operationObject = dynamic_cast<QObject*> (operation);
        if (operationObject != nullptr) {
            const QMetaObject* const mo = operationObject->metaObject();
            if (mo->indexOfSignal(QMetaObject::normalizedSignature("progressChanged(double)")) > -1) {
                ProgressCoordinator::instance()->registerPartProgress(operationObject,
                    SIGNAL(progressChanged(double)), progressOperationSize);
            }
        }
    }

    while (!d->m_performedOperationsCurrentSession.isEmpty()) {
        try {
            Operation *const operation = d->m_performedOperationsCurrentSession.takeLast();
            const bool becameAdmin = !RemoteClient::instance().isActive()
                && operation->value(QLatin1String("admin")).toBool() && gainAdminRights();

            if (operation->value(QLatin1String("uninstall-only")).toBool() &&
                QVariant(value(scRemoveTargetDir)).toBool() &&
                (operation->name() == QLatin1String("Mkdir"))) {
                // We know the mkdir operation which is creating the target path. If we do a
                // full uninstall, prevent a forced remove of the full install path including the
                // target, instead try to remove the target only and only if it is empty,
                // otherwise fail silently. Note: this can only happen if RemoveTargetDir is set,
                // otherwise the operation does not exist at all.
                operation->setValue(QLatin1String("forceremoval"), false);
            }

            PackageManagerCorePrivate::performOperationThreaded(operation, Operation::Undo);

            const QString componentName = operation->value(QLatin1String("component")).toString();
            if (!componentName.isEmpty()) {
                Component *component = componentByName(checkableName(componentName));
                if (!component)
                    component = d->componentsToReplace().value(componentName).second;
                if (component) {
                    component->setUninstalled();
                    d->m_localPackageHub->removePackage(component->name());
                }
            }

            d->m_localPackageHub->writeToDisk();
            if (isInstaller() && d->m_localPackageHub->packageInfoCount() == 0) {
                QFile file(d->m_localPackageHub->fileName());
                if (!file.fileName().isEmpty() && file.exists())
                    file.remove();
            }

            if (becameAdmin)
                dropAdminRights();
        } catch (const Error &e) {
            MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(),
                QLatin1String("ElevationError"), tr("Authentication Error"), tr("Some components "
                "could not be removed completely because administrative rights could not be acquired: %1.")
                .arg(e.message()));
        } catch (...) {
            MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(), QLatin1String("unknown"),
                tr("Unknown error."), tr("Some components could not be removed completely because an unknown "
                "error happened."));
        }
    }
}

/*!
    Returns whether the file extension \a extension is already registered in the
    Windows registry. Returns \c false on all other platforms.

    \sa {installer::isFileExtensionRegistered}{installer.isFileExtensionRegistered}
 */
bool PackageManagerCore::isFileExtensionRegistered(const QString &extension) const
{
    QSettingsWrapper settings(QLatin1String("HKEY_CLASSES_ROOT"), QSettingsWrapper::NativeFormat);
    return settings.value(QString::fromLatin1(".%1/Default").arg(extension)).isValid();
}

/*!
    Returns \c true if the \a filePath exists; otherwise returns \c false.

    \note If the file is a symlink that points to a non existing
     file, \c false is returned.

    \sa {installer::fileExists}{installer.fileExists}

 */
bool PackageManagerCore::fileExists(const QString &filePath) const
{
    return QFileInfo(filePath).exists();
}

/*!
    Returns the contents of the file \a filePath using the encoding specified
    by \a codecName. The file is read in the text mode, that is, end-of-line
    terminators are translated to the local encoding.

    \note If the file does not exist or an error occurs while reading the file, an
     empty string is returned.

    \sa {installer::readFile}{installer.readFile}

 */
QString PackageManagerCore::readFile(const QString &filePath, const QString &codecName) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QTextCodec *codec = QTextCodec::codecForName(qPrintable(codecName));
    if (!codec)
        return QString();

    QTextStream stream(&f);
    stream.setCodec(codec);
    return stream.readAll();
}

/*!
 * Prints \a title to console and reads console input. Function will halt the
 * installer and wait for user input. Returns a line which user has typed into
 * console. The maximum allowed line length is set to \a maxlen. If the stream
 * contains lines longer than this, then the line will be split after maxlen
 * characters. If \a maxlen is 0, the line can be of any length.
 *
 * \note Can be only called when installing from command line instance without GUI.
 * If the output device is not a TTY, i.e. when forwarding to a file, the function
 * will throw an error.
 *
 * \sa {installer::readConsoleLine}{installer.readConsoleLine}
 */
QString PackageManagerCore::readConsoleLine(const QString &title, qint64 maxlen) const
{
    if (!isCommandLineInstance())
        return QString();
    if (LoggingHandler::instance().outputRedirected()) {
        throw Error(tr("User input is required but the output "
            "device is not associated with a terminal."));
    }
    if (!title.isEmpty())
        qDebug() << title;
    QTextStream stream(stdin);
    QString input;
    stream.readLineInto(&input, maxlen);
    return input;
}

/*!
    Returns \a path with the '/' separators converted to separators that are
    appropriate for the underlying operating system.

    On Unix platforms the returned string is the same as the argument.

    \sa {installer::toNativeSeparators}{installer.toNativeSeparators}
    \sa fromNativeSeparators()
*/
QString PackageManagerCore::toNativeSeparators(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

/*!
    Returns \a path using '/' as file separator.

    On Unix platforms the returned string is the same as the argument.

    \sa {installer::fromNativeSeparators}{installer.fromNativeSeparators}
    \sa toNativeSeparators()
*/
QString PackageManagerCore::fromNativeSeparators(const QString &path)
{
    return QDir::fromNativeSeparators(path);
}

/*!
    Checks whether the target directory \a targetDirectory exists and has contents:
    \list
        \li Returns \c true if the directory exists and is empty.
        \li Returns \c false if the directory already exists and contains an installation.
        \li Returns \c false if the target is a file or a symbolic link.
        \li Returns \c true or \c false if the directory exists but is not empty, depending on the
            choice that the end users make in the displayed message box.
    \endlist
*/
bool PackageManagerCore::checkTargetDir(const QString &targetDirectory)
{
    const QDir dir(targetDirectory);
    // the directory exists and is empty...
    if (dir.exists() && dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        return true;

    const QFileInfo fi(targetDirectory);
    if (fi.isDir()) {
        QString fileName = settings().maintenanceToolName();
#if defined(Q_OS_MACOS)
        if (QInstaller::isInBundle(QCoreApplication::applicationDirPath()))
            fileName += QLatin1String(".app/Contents/MacOS/") + fileName;
#elif defined(Q_OS_WIN)
        fileName += QLatin1String(".exe");
#endif

        QFileInfo fi2(targetDirectory + QDir::separator() + fileName);
        if (fi2.exists()) {
            MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(), QLatin1String("TargetDirectoryInUse"),
                tr("Error"), tr("The directory you selected already "
                                "exists and contains an installation. Choose a different target for installation."));
            return false;
        }

        QMessageBox::StandardButton bt =
            MessageBoxHandler::warning(MessageBoxHandler::currentBestSuitParent(), QLatin1String("OverwriteTargetDirectory"),
            tr("Warning"), tr("You have selected an existing, non-empty directory for installation.\nNote that it will be "
                              "completely wiped on uninstallation of this application.\nIt is not advisable to install into "
                              "this directory as installation might fail.\nDo you want to continue?"), QMessageBox::Yes | QMessageBox::No);
        return bt == QMessageBox::Yes;
    } else if (fi.isFile() || fi.isSymLink()) {
        MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(), QLatin1String("WrongTargetDirectory"),
            tr("Error"),  tr("You have selected an existing file "
                             "or symlink, please choose a different target for installation."));
        return false;
    }
    return true;
}

/*!
    Returns a warning if the path to the target directory \a targetDirectory
    is not set or if it is invalid.
*/
QString PackageManagerCore::targetDirWarning(const QString &targetDirectory) const
{
    if (targetDirectory.isEmpty())
        return tr("The installation path cannot be empty, please specify a valid directory.");

    QDir target(targetDirectory);
    if (target.isRelative())
        return tr("The installation path cannot be relative, please specify an absolute path.");

    QString nativeTargetDir = QDir::toNativeSeparators(target.absolutePath());
    if (!settings().allowNonAsciiCharacters()) {
        for (int i = 0; i < nativeTargetDir.length(); ++i) {
            if (nativeTargetDir.at(i).unicode() & 0xff80) {
                return tr("The path or installation directory contains non ASCII characters. This "
                    "is currently not supported! Please choose a different path or installation "
                    "directory.");
            }
        }
    }

    target = target.canonicalPath();
    if (!target.isEmpty() && (target == QDir::root() || target == QDir::home())) {
        return tr("As the install directory is completely deleted, installing in %1 is forbidden.")
            .arg(QDir::toNativeSeparators(target.path()));
    }

#ifdef Q_OS_WIN
    // folder length (set by user) + maintenance tool name length (no extension) + extra padding
    if ((nativeTargetDir.length()
        + settings().maintenanceToolName().length() + 20) >= MAX_PATH) {
        return tr("The path you have entered is too long, please make sure to "
            "specify a valid path.");
    }

    static QRegularExpression reg(QLatin1String(
        "^(?<drive>[a-zA-Z]:\\\\)|"
        "^(\\\\\\\\(?<path>\\w+)\\\\)|"
        "^(\\\\\\\\(?<ip>\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})\\\\)"));
    const QRegularExpressionMatch regMatch = reg.match(nativeTargetDir);

    const QString ipMatch = regMatch.captured(QLatin1String("ip"));
    const QString pathMatch = regMatch.captured(QLatin1String("path"));
    const QString driveMatch = regMatch.captured(QLatin1String("drive"));

    if (ipMatch.isEmpty() && pathMatch.isEmpty() && driveMatch.isEmpty()) {
        return tr("The path you have entered is not valid, please make sure to "
            "specify a valid target.");
    }

    if (!driveMatch.isEmpty()) {
        bool validDrive = false;
        const QFileInfo drive(driveMatch);
        foreach (const QFileInfo &driveInfo, QDir::drives()) {
            if (drive == driveInfo) {
                validDrive = true;
                break;
            }
        }
        if (!validDrive) {  // right now we can only verify local drives
            return tr("The path you have entered is not valid, please make sure to "
                "specify a valid drive.");
        }
        nativeTargetDir = nativeTargetDir.mid(2);
    }

    if (nativeTargetDir.endsWith(QLatin1Char('.')))
        return tr("The installation path must not end with '.', please specify a valid directory.");

    QString ambiguousChars = QLatin1String("[\"~<>|?*!@#$%^&:,; ]"
        "|(\\\\CON)(\\\\|$)|(\\\\PRN)(\\\\|$)|(\\\\AUX)(\\\\|$)|(\\\\NUL)(\\\\|$)|(\\\\COM\\d)(\\\\|$)|(\\\\LPT\\d)(\\\\|$)");
#else // Q_OS_WIN
    QString ambiguousChars = QStringLiteral("[~<>|?*!@#$%^&:,; \\\\]");
#endif // Q_OS_WIN

    if (settings().allowSpaceInPath())
        ambiguousChars.remove(QLatin1Char(' '));

    static QRegularExpression ambCharRegEx(ambiguousChars, QRegularExpression::CaseInsensitiveOption);
    // check if there are not allowed characters in the target path
    QRegularExpressionMatch match = ambCharRegEx.match(nativeTargetDir);
    if (match.hasMatch()) {
        return tr("The installation path must not contain \"%1\", "
            "please specify a valid directory.").arg(match.captured(0));
    }

    return QString();
}

// -- QInstaller

/*!
    Used by operation runner to get a fake installer.
*/
PackageManagerCore::PackageManagerCore()
    : d(new PackageManagerCorePrivate(this))
{
    //TODO: Can be removed if installerbase can do what operation runner does.
    Repository::registerMetaType(); // register, cause we stream the type as QVariant
    qRegisterMetaType<QInstaller::PackageManagerCore::Status>("QInstaller::PackageManagerCore::Status");
    qRegisterMetaType<QInstaller::PackageManagerCore::WizardPage>("QInstaller::PackageManagerCore::WizardPage");
}

/*!
    Creates an installer or uninstaller and performs sanity checks on the operations specified
    by \a operations. A hash table of variables to be stored as package manager core values
    can be specified by \a params. Sets the current instance type to be either a GUI or CLI one based
    on the value of \a commandLineInstance.

    The magic marker \a magicmaker is a \c quint64 that identifies the type of the binary:
    \c installer or \c uninstaller.

    Creates and initializes a remote client. Requests administrator's rights for
    QFile, QSettings, and QProcess operations. Calls \c init() with \a socketName, \a key,
    and \a mode to set the server side authorization key.
*/
PackageManagerCore::PackageManagerCore(qint64 magicmaker, const QList<OperationBlob> &operations,
        const QString &socketName, const QString &key, Protocol::Mode mode,
        const QHash<QString, QString> &params, const bool commandLineInstance)
    : d(new PackageManagerCorePrivate(this, magicmaker, operations))
{
    setCommandLineInstance(commandLineInstance);
    Repository::registerMetaType(); // register, cause we stream the type as QVariant
    qRegisterMetaType<QInstaller::PackageManagerCore::Status>("QInstaller::PackageManagerCore::Status");
    qRegisterMetaType<QInstaller::PackageManagerCore::WizardPage>("QInstaller::PackageManagerCore::WizardPage");

    d->initialize(params);

    // Creates and initializes a remote client, makes us get admin rights for QFile, QSettings
    // and QProcess operations. Init needs to called to set the server side authorization key.
    if (!d->isUpdater() && !d->isReinstaller()) {
        RemoteClient::instance().init(socketName, key, mode, Protocol::StartAs::SuperUser);
        RemoteClient::instance().setAuthorizationFallbackDisabled(settings().disableAuthorizationFallback());
    }

    //
    // Sanity check to detect a broken installations with missing operations.
    // Every installed package should have at least one MinimalProgress operation.
    //
    QSet<QString> installedPackages = d->m_core->localInstalledPackages().keys().toSet();
    QSet<QString> operationPackages;
    foreach (QInstaller::Operation *operation, d->m_performedOperationsOld) {
        if (operation->hasValue(QLatin1String("component")))
            operationPackages.insert(operation->value(QLatin1String("component")).toString());
    }

    QSet<QString> packagesWithoutOperation = installedPackages - operationPackages;
    QSet<QString> orphanedOperations = operationPackages - installedPackages;
    if (!packagesWithoutOperation.isEmpty() || !orphanedOperations.isEmpty())  {
        qCritical() << "Operations missing for installed packages" << packagesWithoutOperation.toList();
        qCritical() << "Orphaned operations" << orphanedOperations.toList();
        qCritical() << "Your installation seems to be corrupted. Please consider re-installing from scratch, "
                       "remove the packages from components.xml which operations are missing, "
                       "or reinstall the packages.";
    } else {
        qCDebug(QInstaller::lcInstallerInstallLog) << "Operations sanity check succeeded.";
    }
    connect(this, &PackageManagerCore::metaJobProgress,
            ProgressCoordinator::instance(), &ProgressCoordinator::printProgressPercentage);
    connect(this, &PackageManagerCore::metaJobInfoMessage,
            ProgressCoordinator::instance(), &ProgressCoordinator::printProgressMessage);
}

/*!
    Destroys the instance.
*/
PackageManagerCore::~PackageManagerCore()
{
    if (!isUninstaller() && !(isInstaller() && status() == PackageManagerCore::Canceled)) {
        QDir targetDir(value(scTargetDir));
        QString logFileName = targetDir.absoluteFilePath(value(QLatin1String("LogFileName"),
            QLatin1String("InstallationLog.txt")));
        QInstaller::VerboseWriter::instance()->setFileName(logFileName);
    }
    delete d;

    try {
        PlainVerboseWriterOutput plainOutput;
        if (!VerboseWriter::instance()->flush(&plainOutput)) {
            VerboseWriterAdminOutput adminOutput(this);
            VerboseWriter::instance()->flush(&adminOutput);
        }
    } catch (...) {
        // Intentionally left blank; don't permit exceptions from VerboseWriter
        // to escape destructor.
    }

    RemoteClient::instance().setActive(false);
    RemoteClient::instance().destroy();

    QMutexLocker _(globalVirtualComponentsFontMutex());
    delete sVirtualComponentsFont;
    sVirtualComponentsFont = nullptr;
}

/* static */
/*!
    Returns the virtual components' font.
*/
QFont PackageManagerCore::virtualComponentsFont()
{
    QMutexLocker _(globalVirtualComponentsFontMutex());
    if (!sVirtualComponentsFont)
        sVirtualComponentsFont = new QFont;
    return *sVirtualComponentsFont;
}

/* static */
/*!
    Sets the virtual components' font to \a font.
*/
void PackageManagerCore::setVirtualComponentsFont(const QFont &font)
{
    QMutexLocker _(globalVirtualComponentsFontMutex());
    if (sVirtualComponentsFont)
        delete sVirtualComponentsFont;
    sVirtualComponentsFont = new QFont(font);
}

/* static */
/*!
    Returns \c true if virtual components are visible.
*/
bool PackageManagerCore::virtualComponentsVisible()
{
    return sVirtualComponentsVisible;
}

/* static */
/*!
    Shows virtual components if \a visible is \c true.
*/
void PackageManagerCore::setVirtualComponentsVisible(bool visible)
{
    sVirtualComponentsVisible = visible;
}

/* static */
/*!
    Returns \c true if forced installation is not set for all components for
    which the <ForcedInstallation> element is set in the package information
    file.
*/
bool PackageManagerCore::noForceInstallation()
{
    return sNoForceInstallation;
}

/* static */
/*!
    Overwrites the value specified for the component in the \c <ForcedInstallation>
    element in the package information file with \a value. This enables end users
    to select or deselect the component in the installer.
*/
void PackageManagerCore::setNoForceInstallation(bool value)
{
    sNoForceInstallation = value;
}

/* static */
/*!
    Returns \c true if components are not selected by default although
    \c <Default> element is set in the package information file.
*/
bool PackageManagerCore::noDefaultInstallation()
{
    return sNoDefaultInstallation;
}

/* static */
/*!
    Overwrites the value specified for the component in the \c <Default>
    element in the package information file with \a value. Setting \a value
    to \c true unselects the components.
*/
void PackageManagerCore::setNoDefaultInstallation(bool value)
{
    sNoDefaultInstallation = value;
}
/* static */
/*!
    Returns \c true if a local repository should be created from binary content.
*/
bool PackageManagerCore::createLocalRepositoryFromBinary()
{
    return sCreateLocalRepositoryFromBinary;
}

/* static */
/*!
    Determines that a local repository should be created from binary content if
    \a create is \c true.
*/
void PackageManagerCore::setCreateLocalRepositoryFromBinary(bool create)
{
    sCreateLocalRepositoryFromBinary = create;
}

/*!
    Returns \c true if the package manager is running and installed packages are
    found. Otherwise, returns \c false.
*/
bool PackageManagerCore::fetchLocalPackagesTree()
{
    d->setStatus(Running);

    if (!isPackageManager()) {
        d->setStatus(Failure, tr("Application not running in Package Manager mode."));
        return false;
    }

    LocalPackagesHash installedPackages = d->localInstalledPackages();
    if (installedPackages.isEmpty()) {
        if (status() != Failure)
            d->setStatus(Failure, tr("No installed packages found."));
        return false;
    }

    emit startAllComponentsReset();

    d->clearAllComponentLists();
    QHash<QString, QInstaller::Component*> components;

    const QStringList &keys = installedPackages.keys();
    foreach (const QString &key, keys) {
        QScopedPointer<QInstaller::Component> component(new QInstaller::Component(this));
        component->loadDataFromPackage(installedPackages.value(key));
        const QString &name = component->treeName();
        if (components.contains(name)) {
            d->setStatus(Failure, tr("Cannot register component! Component with identifier %1 "
                                     "already exists.").arg(name));
            return false;
        }
        components.insert(name, component.take());
    }

    if (!d->buildComponentTree(components, false))
        return false;

    updateDisplayVersions(scDisplayVersion);

    emit finishAllComponentsReset(d->m_rootComponents);
    d->setStatus(Success);

    return true;
}

/*!
    Returns a list of local installed packages. The list can be empty.
*/
LocalPackagesHash PackageManagerCore::localInstalledPackages()
{
    return d->localInstalledPackages();
}

/*!
    Emits the coreNetworkSettingsChanged() signal when network settings change.
*/
void PackageManagerCore::networkSettingsChanged()
{
    cancelMetaInfoJob();

    d->m_updates = false;
    d->m_repoFetched = false;
    d->m_updateSourcesAdded = false;

    if (isMaintainer() ) {
        bool gainedAdminRights = false;
        if (!directoryWritable(d->targetDir())) {
            gainAdminRights();
            gainedAdminRights = true;
        }
        d->writeMaintenanceConfigFiles();
        if (gainedAdminRights)
            dropAdminRights();
    }

    KDUpdater::FileDownloaderFactory::instance().setProxyFactory(proxyFactory());

    emit coreNetworkSettingsChanged();
}

/*!
    Returns a copy of the proxy factory that the package manager uses to determine
    the proxies to be used for requests.
*/
PackageManagerProxyFactory *PackageManagerCore::proxyFactory() const
{
    if (d->m_proxyFactory)
        return d->m_proxyFactory->clone();
    return new PackageManagerProxyFactory(this);
}

/*!
    Sets the proxy factory for the package manager to be \a factory. A proxy factory
    is used to determine a more specific list of proxies to be used for a given
    request, instead of trying to use the same proxy value for all requests. This
    might only be of use for HTTP or FTP requests.
*/
void PackageManagerCore::setProxyFactory(PackageManagerProxyFactory *factory)
{
    delete d->m_proxyFactory;
    d->m_proxyFactory = factory;
    KDUpdater::FileDownloaderFactory::instance().setProxyFactory(proxyFactory());
}

/*!
    Returns a list of packages available in all the repositories that were
    looked at.
*/
PackagesList PackageManagerCore::remotePackages()
{
    return d->remotePackages();
}

/*!
    Checks for compressed packages to install. Returns \c true if newer versions exist
    and they can be installed.
*/
bool PackageManagerCore::fetchCompressedPackagesTree()
{
    const LocalPackagesHash installedPackages = d->localInstalledPackages();
    if (!isInstaller() && status() == Failure)
        return false;

    if (!d->fetchMetaInformationFromRepositories(DownloadType::CompressedPackage))
        return false;

    if (!d->addUpdateResourcesFromRepositories(true, true)) {
        return false;
    }

    const PackagesList &packages = d->remotePackages();
    if (packages.isEmpty())
        return false;

    return fetchPackagesTree(packages, installedPackages);
}

/*!
    Checks for packages to install. Returns \c true if newer versions exist
    and they can be installed.
*/
bool PackageManagerCore::fetchRemotePackagesTree()
{
    d->setStatus(Running);

    if (isUninstaller()) {
        d->setStatus(Failure, tr("Application running in Uninstaller mode."));
        return false;
    }

    if (!ProductKeyCheck::instance()->hasValidKey()) {
        d->setStatus(Failure, ProductKeyCheck::instance()->lastErrorString());
        return false;
    }

    const LocalPackagesHash installedPackages = d->localInstalledPackages();
    if (!isInstaller() && status() == Failure)
        return false;

    if (!d->fetchMetaInformationFromRepositories())
        return false;

    if (!d->fetchMetaInformationFromRepositories(DownloadType::CompressedPackage))
        return false;

    if (!d->addUpdateResourcesFromRepositories(true))
        return false;

    const PackagesList &packages = d->remotePackages();
    if (packages.isEmpty())
        return false;

    return fetchPackagesTree(packages, installedPackages);
}

bool PackageManagerCore::fetchPackagesTree(const PackagesList &packages, const LocalPackagesHash installedPackages) {

    bool success = false;
    if (!isUpdater() && !isReinstaller()) {
        success = fetchAllPackages(packages, installedPackages);
        if (d->statusCanceledOrFailed())
            return false;
        if (success && isPackageManager()) {
            foreach (Package *const update, packages) {
                bool essentialUpdate = (update->data(scEssential, scFalse).toString().toLower() == scTrue);
                bool forcedUpdate = (update->data(scForcedUpdate, scFalse).toString().toLower() == scTrue);
                if (essentialUpdate || forcedUpdate) {
                    const QString name = update->data(scName).toString();
                    // 'Essential' package not installed, install.
                    if (essentialUpdate && !installedPackages.contains(name)) {
                        success = false;
                        continue;
                    }
                    // 'Forced update' package  not installed, no update needed
                    if (forcedUpdate && !installedPackages.contains(name))
                        continue;

                    const LocalPackage localPackage = installedPackages.value(name);
                    if (!d->packageNeedsUpdate(localPackage, update))
                        continue;

                    const QDate updateDate = update->data(scReleaseDate).toDate();
                    if (localPackage.lastUpdateDate >= updateDate)
                        continue;  // remote release date equals or is less than the installed maintenance tool

                    success = false;
                    break;  // we found a newer version of the forced/essential update package
                }
            }

            if (!success && !d->statusCanceledOrFailed()) {
                updateDisplayVersions(scRemoteDisplayVersion);
                d->setStatus(ForceUpdate, tr("There is an important update available, please run the "
                    "updater first."));
                return false;
            }
        }
    } else if(isUpdater()) {
        success = fetchUpdaterPackages(packages, installedPackages);
    }
    else {
        success = fetchReinstallerPackages(packages, installedPackages);
    }

    updateDisplayVersions(scRemoteDisplayVersion);

    if (success && !d->statusCanceledOrFailed())
        d->setStatus(Success);
    emit componentsRecalculated();
    return success;
}

/*!
    \fn QInstaller::PackageManagerCore::addWizardPage(QInstaller::Component * component, const QString & name, int page)

    Adds the widget with object name \a name registered by \a component as a new page
    into the installer's GUI wizard. The widget is added before \a page.

    See \l{Controller Scripting} for the possible values of \a page.

    Returns \c true if the operation succeeded.

    \sa {installer::addWizardPage}{installer.addWizardPage}
*/
bool PackageManagerCore::addWizardPage(Component *component, const QString &name, int page)
{
    if (!isCommandLineInstance()) {
        if (QWidget* const widget = component->userInterface(name)) {
            emit wizardPageInsertionRequested(widget, static_cast<WizardPage>(page));
            return true;
        }
    } else {
        qCDebug(QInstaller::lcDeveloperBuild) << "Headless installation: skip wizard page addition: " << name;
    }
    return false;
}

/*!
    \fn QInstaller::PackageManagerCore::removeWizardPage(QInstaller::Component * component, const QString & name)

    Removes the widget with the object name \a name previously added to the installer's wizard
    by \a component.

    Returns \c true if the operation succeeded.

    \sa {installer::removeWizardPage}{installer.removeWizardPage}
    \sa addWizardPage(), setDefaultPageVisible(), wizardPageRemovalRequested()
*/
bool PackageManagerCore::removeWizardPage(Component *component, const QString &name)
{
    if (!isCommandLineInstance()) {
        if (QWidget* const widget = component->userInterface(name)) {
            emit wizardPageRemovalRequested(widget);
            return true;
        }
    } else {
        qCDebug(QInstaller::lcDeveloperBuild) << "Headless installation: skip wizard page removal: " << name;
    }
    return false;
}

/*!
    Sets the visibility of the default page with the ID \a page to \a visible. That is,
    removes it from or adds it to the wizard. This works only for pages that were
    in the installer when it was started.

    Returns \c true.

    \sa {installer::setDefaultPageVisible}{installer.setDefaultPageVisible}
    \sa addWizardPage(), removeWizardPage()
 */
bool PackageManagerCore::setDefaultPageVisible(int page, bool visible)
{
    emit wizardPageVisibilityChangeRequested(visible, page);
    return true;
}

/*!
    \fn QInstaller::PackageManagerCore::setValidatorForCustomPage(QInstaller::Component * component, const QString & name, const QString & callbackName)

    Sets a validator for the custom page specified by \a name and \a callbackName
    for the component \a component.

    When using this, \a name has to match a dynamic page starting with \c Dynamic. For example, if the page
    is called DynamicReadyToInstallWidget, then \a name should be set to \c ReadyToInstallWidget. The
    \a callbackName should be set to a function that returns a boolean. When the \c Next button is pressed
    on the custom page, then it will call the \a callbackName function. If this returns \c true, then it will
    move to the next page.

    \sa {installer::setValidatorForCustomPage}{installer.setValidatorForCustomPage}
    \sa setValidatorForCustomPageRequested()
 */
void PackageManagerCore::setValidatorForCustomPage(Component *component, const QString &name,
                                                   const QString &callbackName)
{
    emit setValidatorForCustomPageRequested(component, name, callbackName);
}

/*!
    Selects the component with \a id.
    \sa {installer::selectComponent}{installer.selectComponent}
    \sa deselectComponent()
*/
void PackageManagerCore::selectComponent(const QString &id)
{
    d->setComponentSelection(id, Qt::Checked);
}

/*!
    Deselects the component with \a id.
    \sa {installer::deselectComponent}{installer.deselectComponent}
    \sa selectComponent()
*/
void PackageManagerCore::deselectComponent(const QString &id)
{
    d->setComponentSelection(id, Qt::Unchecked);
}

/*!
    \fn QInstaller::PackageManagerCore::addWizardPageItem(QInstaller::Component * component, const QString & name,
        int page, int position)

    Adds the widget with the object name \a name registered by \a component as a GUI element
    into the installer's GUI wizard. The widget is added on \a page ordered by
    \a position number. If several widgets are added to the same page, the widget
    with lower \a position number will be inserted on top.

    See \l{Controller Scripting} for the possible values of \a page.

    If the widget can be found in an UI file for the component, returns \c true and emits the
    wizardWidgetInsertionRequested() signal.

    \sa {installer::addWizardPageItem}{installer.addWizardPageItem}
    \sa removeWizardPageItem(), wizardWidgetInsertionRequested()
*/
bool PackageManagerCore::addWizardPageItem(Component *component, const QString &name, int page, int position)
{
    if (!isCommandLineInstance()) {
        if (QWidget* const widget = component->userInterface(name)) {
            emit wizardWidgetInsertionRequested(widget, static_cast<WizardPage>(page), position);
            return true;
        }
    } else {
        qCDebug(QInstaller::lcDeveloperBuild) << "Headless installation: skip wizard page item addition: " << name;
    }
    return false;
}

/*!
    \fn QInstaller::PackageManagerCore::removeWizardPageItem(QInstaller::Component * component, const QString & name)

    Removes the widget with the object name \a name previously added to the installer's wizard
    by \a component.

    If the widget can be found in an UI file for the component, returns \c true and emits the
    wizardWidgetRemovalRequested() signal.

    \sa {installer::removeWizardPageItem}{installer.removeWizardPageItem}
    \sa addWizardPageItem()
*/
bool PackageManagerCore::removeWizardPageItem(Component *component, const QString &name)
{
    if (!isCommandLineInstance()) {
        if (QWidget* const widget = component->userInterface(name)) {
            emit wizardWidgetRemovalRequested(widget);
            return true;
        }
    }
    return false;
}

/*!
    Registers additional \a repositories.

    \sa {installer::addUserRepositories}{installer.addUserRepositories}
    \sa setTemporaryRepositories()
 */
void PackageManagerCore::addUserRepositories(const QStringList &repositories)
{
    QSet<Repository> repositorySet;
    foreach (const QString &repository, repositories)
        repositorySet.insert(Repository::fromUserInput(repository));

    settings().addUserRepositories(repositorySet);
}

/*!
    Sets additional \a repositories for this instance of the installer or updater
    if \a replace is \c false. \a compressed repositories can be added as well.
    Will be removed after invoking it again.

    \sa {installer::setTemporaryRepositories}{installer.setTemporaryRepositories}
    \sa addUserRepositories()
*/
void PackageManagerCore::setTemporaryRepositories(const QStringList &repositories, bool replace,
                                                  bool compressed)
{
    QSet<Repository> repositorySet;
    foreach (const QString &repository, repositories)
        repositorySet.insert(Repository::fromUserInput(repository, compressed));
    settings().setTemporaryRepositories(repositorySet, replace);
}

/*!
    Checks whether the downloader should try to download SHA-1 checksums for
    archives and returns the checksums.
*/
bool PackageManagerCore::testChecksum() const
{
    return d->m_testChecksum;
}

/*!
    The \a test argument determines whether the downloader should try to
    download SHA-1 checksums for archives.
*/
void PackageManagerCore::setTestChecksum(bool test)
{
    d->m_testChecksum = test;
}

/*!
    Returns the script engine that prepares and runs the component scripts.

    \sa {Component Scripting}
*/
ScriptEngine *PackageManagerCore::componentScriptEngine() const
{
    return d->componentScriptEngine();
}

/*!
    Returns the script engine that prepares and runs the control script.

    \sa {Controller Scripting}
*/
ScriptEngine *PackageManagerCore::controlScriptEngine() const
{
    return d->controlScriptEngine();
}

/*!
    Appends \a component as the root component to the internal storage for
    installer or package manager components. To append a component as a child to
    an already existing component, use Component::appendComponent(). Emits
    the componentAdded() signal.
*/
void PackageManagerCore::appendRootComponent(Component *component)
{
    d->m_rootComponents.append(component);
    emit componentAdded(component);
}

/*!
    \enum PackageManagerCore::ComponentType
    \brief This enum holds the type of the component list to be returned:

    \value  Root
            Returns a list of root components.
    \value  Descendants
            Returns a list of all descendant components. In updater mode the
            list is empty, because component updates cannot have children.
    \value  Dependencies
            Returns a list of all available dependencies when run as updater.
            When running as installer, package manager, or uninstaller, this
            will always result in an empty list.
    \value  Replacements
            Returns a list of all available replacement components relevant to
            the run mode.
    \value  AllNoReplacements
            Returns a list of available components, including root, descendant,
            and dependency components relevant to the run mode.
    \value  All
            Returns a list of all available components, including root,
            descendant, dependency, and replacement components relevant to the
            run mode.
*/

/*!
    \typedef PackageManagerCore::ComponentTypes

    Synonym for QList<Component>.

*/

/*!
    Returns a list of components depending on the component types passed in \a mask.
    Optionally, a \a regexp expression can be used to further filter the listed packages.
*/
QList<Component *> PackageManagerCore::components(ComponentTypes mask, const QString &regexp) const
{
    QList<Component *> components;

    const bool updater = isUpdater();
    const bool reinstaller = isReinstaller();

    if (mask.testFlag(ComponentType::Root)) {
        if (updater) {
            components += d->m_updaterComponents;
        }
        else if (reinstaller) {
            components += d->m_reinstallerComponents;
        }
        else {
            components += d->m_rootComponents;
        }
    }
    if (mask.testFlag(ComponentType::Replacements)) {
        if (updater) {
            components += d->m_updaterDependencyReplacements;
        }
        else if (reinstaller) {
            components += d->m_reinstallerDependencyReplacements;
        }
        else {
            components += d->m_rootDependencyReplacements;
        }
    }


    if (updater) {
        if (mask.testFlag(ComponentType::Dependencies))
            components.append(d->m_updaterComponentsDeps);
        // No descendants here, updates are always a flat list and cannot have children!
    }
    else if (reinstaller) {
        if (mask.testFlag(ComponentType::Dependencies))
            components.append(d->m_reinstallerComponentsDeps);
        // No descendants here, updates are always a flat list and cannot have children!
    }
    else {
        if (mask.testFlag(ComponentType::Descendants)) {
            foreach (QInstaller::Component *component, d->m_rootComponents)
                components += component->descendantComponents();
        }
    } 
    

    if (!regexp.isEmpty()) {
        QRegularExpression re(regexp);
        QList<Component*>::iterator iter = components.begin();
        while (iter != components.end()) {
            if (!re.match(iter.i->t()->name()).hasMatch())
                iter = components.erase(iter);
            else
                iter++;
        }
    }

    return components;
}

/*!
    Appends \a component to the internal storage for updater components. Emits
    the componentAdded() signal.
*/
void PackageManagerCore::appendUpdaterComponent(Component *component)
{
    component->setUpdateAvailable(true);
    d->m_updaterComponents.append(component);
    emit componentAdded(component);
}

void PackageManagerCore::appendReinstallerComponent(Component* component)
{
    component->setUpdateAvailable(true);
    d->m_reinstallerComponents.append(component);
    emit componentAdded(component);
}

/*!
    Returns a component matching \a name. \a name can also contain a version requirement.
    For example, \c org.qt-project.sdk.qt returns any component with that name,
    whereas \c{org.qt-project.sdk.qt->=4.5} requires the returned component to have
    at least version 4.5. If no component matches the requirement, \c 0 is returned.
*/
Component *PackageManagerCore::componentByName(const QString &name) const
{
    return componentByName(name, components(ComponentType::AllNoReplacements));
}

/*!
    Searches \a components for a component matching \a name and returns it.
    \a name can also contain a version requirement. For example, \c org.qt-project.sdk.qt
    returns any component with that name, whereas \c{org.qt-project.sdk.qt->=4.5} requires
    the returned component to have at least version 4.5. If no component matches the
    requirement, \c 0 is returned.
*/
Component *PackageManagerCore::componentByName(const QString &name, const QList<Component *> &components)
{
    if (name.isEmpty())
        return nullptr;

    QString fixedVersion;
    QString fixedName;

    parseNameAndVersion(name, &fixedName, &fixedVersion);

    foreach (Component *component, components) {
        if (componentMatches(component, fixedName, fixedVersion))
            return component;
    }

    return nullptr;
}

/*!
    Returns \c true if directory specified by \a path is writable by
    the current user.
*/
bool PackageManagerCore::directoryWritable(const QString &path) const
{
    return d->directoryWritable(path);
}

/*!
    Returns a list of components that are marked for installation. The list can
    be empty.
*/
QList<Component *> PackageManagerCore::componentsMarkedForInstallation() const
{
    QList<Component*> markedForInstallation;
    const QList<Component*> relevant = components(ComponentType::Root | ComponentType::Descendants);
    if (isUpdater() || isReinstaller()) {
        foreach (Component *component, relevant) {
            if (component->updateRequested())
                markedForInstallation.append(component);
        }
    } else {
        // relevant means all components which are not replaced
        foreach (Component *component, relevant) {
            // ask for all components which will be installed to get all dependencies
            // even dependencies which are changed without an increased version
            if (component->isSelectedForInstallation()
                    || (component->isInstalled()
                    && !component->uninstallationRequested())) {
                markedForInstallation.append(component);
            }
        }
    }
    return markedForInstallation;
}

/*!
    Determines which components to install based on the current run mode and returns an
    ordered list of components to install. Also auto installed dependencies are resolved.
    The aboutCalculateComponentsToInstall() signal is emitted
    before the calculation starts, the finishedCalculateComponentsToInstall()
    signal once all calculations are done.

    \sa {installer::calculateComponentsToInstall}{installer.calculateComponentsToInstall}

*/
bool PackageManagerCore::calculateComponentsToInstall() const
{
    emit aboutCalculateComponentsToInstall();
    if (!d->m_componentsToInstallCalculated) {
        d->clearInstallerCalculator();
        QList<Component*> selectedComponentsToInstall = componentsMarkedForInstallation();

        d->storeCheckState();
        d->m_componentsToInstallCalculated =
            d->installerCalculator()->appendComponentsToInstall(selectedComponentsToInstall);
    }
    emit finishedCalculateComponentsToInstall();
    return d->m_componentsToInstallCalculated;
}

/*!
    Returns an ordered list of components to install. The list can be empty.
*/
QList<Component*> PackageManagerCore::orderedComponentsToInstall() const
{
    return d->installerCalculator()->orderedComponentsToInstall();
}

/*!
    Calculates components to install and uninstall. In case of an error, returns \c false
    and and sets the \a displayString for error detail.
*/

bool PackageManagerCore::calculateComponents(QString *displayString)
{
    QString htmlOutput;
    QString lastInstallReason;
    if (!calculateComponentsToUninstall() ||
            !calculateComponentsToInstall()) {
        htmlOutput.append(QString::fromLatin1("<h2><font color=\"red\">%1</font></h2><ul>")
                          .arg(tr("Cannot resolve all dependencies.")));
        //if we have a missing dependency or a recursion we can display it
        if (!componentsToInstallError().isEmpty()) {
            htmlOutput.append(QString::fromLatin1("<li> %1 </li>").arg(
                                  componentsToInstallError()));
        }
        htmlOutput.append(QLatin1String("</ul>"));
        if (displayString)
            *displayString = htmlOutput;
        return false;
    }

    // In case of updater mode we don't uninstall components.
    if (!isUpdater() && !isReinstaller()) {
        QList<Component*> componentsToRemove = componentsToUninstall();
        if (!componentsToRemove.isEmpty()) {
            htmlOutput.append(QString::fromLatin1("<h3>%1</h3><ul>").arg(tr("Components about to "
                "be removed.")));
            foreach (Component *component, componentsToRemove)
                htmlOutput.append(QString::fromLatin1("<li> %1 </li>").arg(component->name()));
            htmlOutput.append(QLatin1String("</ul>"));
        }
    }

    foreach (Component *component, orderedComponentsToInstall()) {
        const QString reason = installReason(component);
        if (lastInstallReason != reason) {
            if (!lastInstallReason.isEmpty()) // means we had to close the previous list
                htmlOutput.append(QLatin1String("</ul>"));
            htmlOutput.append(QString::fromLatin1("<h3>%1</h3><ul>").arg(reason));
            lastInstallReason = reason;
        }
        htmlOutput.append(QString::fromLatin1("<li> %1 </li>").arg(component->name()));
    }
    if (displayString)
        *displayString = htmlOutput;
    return true;
}

/*!
    Calculates a list of components to uninstall based on the current run mode.
    The aboutCalculateComponentsToUninstall() signal is emitted
    before the calculation starts, the finishedCalculateComponentsToUninstall() signal once all
    calculations are done. Always returns \c true.

    \sa {installer::calculateComponentsToUninstall}{installer.calculateComponentsToUninstall}
*/
bool PackageManagerCore::calculateComponentsToUninstall() const
{
    emit aboutCalculateComponentsToUninstall();
    if (!isUpdater() && !isReinstaller()) {
        // hack to avoid removing needed dependencies
        QSet<Component*>  componentsToInstall = d->installerCalculator()->orderedComponentsToInstall().toSet();

        QList<Component*> componentsToUninstall;
        foreach (Component *component, components(ComponentType::All)) {
            if (component->uninstallationRequested() && !componentsToInstall.contains(component))
                componentsToUninstall.append(component);
        }

        d->clearUninstallerCalculator();
        d->storeCheckState();
        d->uninstallerCalculator()->appendComponentsToUninstall(componentsToUninstall);
    }
    emit finishedCalculateComponentsToUninstall();
    return true;
}

/*!
    Returns a human-readable description of the error that occurred when
    evaluating the components to install. The error message is empty if no error
    occurred.

    \sa calculateComponentsToInstall
*/
QList<Component *> PackageManagerCore::componentsToUninstall() const
{
    return d->uninstallerCalculator()->componentsToUninstall().toList();
}

/*!
    Returns errors found in the components that are marked for installation.
*/
QString PackageManagerCore::componentsToInstallError() const
{
    return d->installerCalculator()->componentsToInstallError();
}

/*!
    Returns the reason why \a component needs to be installed:

    \list
        \li The component was scheduled for installation.
        \li The component was added as a dependency for another component.
        \li The component was added as an automatic dependency.
    \endlist
*/
QString PackageManagerCore::installReason(Component *component) const
{
    return d->installerCalculator()->installReason(component);
}

/*!
    Returns a list of components that depend on \a _component. The list can be
    empty.

    \note Automatic dependencies are not resolved.
*/
QList<Component*> PackageManagerCore::dependees(const Component *_component) const
{
    if (!_component)
        return QList<Component *>();

    const QList<QInstaller::Component *> availableComponents = components(ComponentType::All);
    if (availableComponents.isEmpty())
        return QList<Component *>();

    QList<Component *> dependees;
    QString name;
    QString version;
    foreach (Component *component, availableComponents) {
        const QStringList &dependencies = component->dependencies();
        foreach (const QString &dependency, dependencies) {
            parseNameAndVersion(dependency, &name, &version);
            if (componentMatches(_component, name, version))
                dependees.append(component);
        }
    }
    return dependees;
}

/*!
    Returns the default component model.
*/
ComponentModel *PackageManagerCore::defaultComponentModel() const
{
    QMutexLocker _(globalModelMutex());
    if (!d->m_defaultModel) {
        d->m_defaultModel = componentModel(const_cast<PackageManagerCore*> (this),
            QLatin1String("AllComponentsModel"));
    }
    connect(this, &PackageManagerCore::finishAllComponentsReset, d->m_defaultModel,
        &ComponentModel::setRootComponents);
    return d->m_defaultModel;
}

/*!
    Returns the updater component model.
*/
ComponentModel *PackageManagerCore::updaterComponentModel() const
{
    QMutexLocker _(globalModelMutex());
    if (!d->m_updaterModel) {
        d->m_updaterModel = componentModel(const_cast<PackageManagerCore*> (this),
            QLatin1String("UpdaterComponentsModel"));
    }
    connect(this, &PackageManagerCore::finishUpdaterComponentsReset, d->m_updaterModel,
        &ComponentModel::setRootComponents);
    return d->m_updaterModel;
}

/*!
    Returns the reinstaller component model.
*/
ComponentModel* PackageManagerCore::reinstallerComponentModel() const
{
    QMutexLocker _(globalModelMutex());
    if (!d->m_reinstallerModel) {
        d->m_reinstallerModel = componentModel(const_cast<PackageManagerCore*> (this),
            QLatin1String("ReinstallerComponentsModel"));
    }
    connect(this, &PackageManagerCore::finishReinstallerComponentsReset, d->m_reinstallerModel,
        &ComponentModel::setRootComponents);
    return d->m_reinstallerModel;
}




/*!
    Lists available packages filtered with \a regexp without GUI. Virtual
    components are not listed unless set visible. Optionally, a \a filters
    hash containing package information elements and regular expressions
    can be used to further filter listed packages.

    \sa setVirtualComponentsVisible()
*/
void PackageManagerCore::listAvailablePackages(const QString &regexp, const QHash<QString, QString> &filters)
{
    setPackageViewer();
    qCDebug(QInstaller::lcInstallerInstallLog)
        << "Searching packages with regular expression:" << regexp;

    ComponentModel *model = defaultComponentModel();
    d->fetchMetaInformationFromRepositories(DownloadType::UpdatesXML);

    d->addUpdateResourcesFromRepositories(true);
    QRegularExpression re(regexp);
    const PackagesList &packages = d->remotePackages();
    if (!fetchAllPackages(packages, LocalPackagesHash())) {
        qCWarning(QInstaller::lcInstallerInstallLog)
            << "There was a problem with loading the package data.";
        return;
    }

    PackagesList matchedPackages;
    foreach (Package *package, packages) {
        const QString name = package->data(scName).toString();
        Component *component = componentByName(name);
        if (!component)
            continue;

        const QModelIndex &idx = model->indexFromComponentName(component->treeName());
        if (idx.isValid() && re.match(name).hasMatch()) {
            bool ignoreComponent = false;
            for (auto &key : filters.keys()) {
                const QString elementValue = component->value(key);
                QRegularExpression elementRegexp(filters.value(key));
                if (elementValue.isEmpty() || !elementRegexp.match(elementValue).hasMatch()) {
                    ignoreComponent = true;
                    break;
                }
            }
            if (!ignoreComponent)
                matchedPackages.append(package);
        }
    }
    if (matchedPackages.count() == 0)
        qCDebug(QInstaller::lcInstallerInstallLog) << "No matching packages found.";
    else
        LoggingHandler::instance().printPackageInformation(matchedPackages, localInstalledPackages());
}

bool PackageManagerCore::componentUninstallableFromCommandLine(const QString &componentName)
{
    // We will do a recursive check for every child this component has.
    Component *component = componentByName(componentName);
    const QList<Component*> childComponents = component->childItems();
    foreach (const Component *childComponent, childComponents) {
        if (!componentUninstallableFromCommandLine(childComponent->name()))
            return false;
    }
    ComponentModel *model = defaultComponentModel();
    const QModelIndex &idx = model->indexFromComponentName(component->treeName());
    if (model->data(idx, Qt::CheckStateRole) == QVariant::Invalid) {
        // Component cannot be unselected, check why
        if (component->forcedInstallation()) {
            qCWarning(QInstaller::lcInstallerInstallLog).noquote().nospace()
                << "Cannot uninstall ForcedInstallation component " << component->name();
        } else if (component->autoDependencies().count() > 0) {
            qCWarning(QInstaller::lcInstallerInstallLog).noquote().nospace() << "Cannot uninstall component "
                << componentName << " because it is added as auto dependency to "
                << component->autoDependencies().join(QLatin1Char(','));
        } else if (component->isVirtual() && !virtualComponentsVisible()) {
            qCWarning(QInstaller::lcInstallerInstallLog).noquote().nospace()
                << "Cannot uninstall virtual component " << component->name();
        } else {
            qCWarning(QInstaller::lcInstallerInstallLog).noquote().nospace()
                << "Cannot uninstall component " << component->name();
        }
        return false;
    }
    return true;
}

/*!
    \internal

    Tries to set \c Qt::CheckStateRole to \c Qt::Checked for given \a components in the
    default component model. Returns \c true if \a components contains at least one component
    eligible for installation, otherwise returns \c false. An error message can be retrieved
    with \a errorMessage.
*/
bool PackageManagerCore::checkComponentsForInstallation(const QStringList &components, QString &errorMessage)
{
    bool installComponentsFound = false;

    ComponentModel *model = defaultComponentModel();
    foreach (const QString &name, components) {
        Component *component = componentByName(name);
        if (!component) {
            errorMessage.append(tr("Cannot install %1. Component not found.\n").arg(name));
            continue;
        }
        const QModelIndex &idx = model->indexFromComponentName(component->treeName());
        if (idx.isValid()) {
            if ((model->data(idx, Qt::CheckStateRole) == QVariant::Invalid) && !component->forcedInstallation()) {
                // User cannot select the component, check why
                if (component->autoDependencies().count() > 0) {
                    errorMessage.append(tr("Cannot install component %1. Component is installed only as automatic "
                        "dependency to %2.\n").arg(name, component->autoDependencies().join(QLatin1Char(','))));
                } else if (!component->isCheckable()) {
                    errorMessage.append(tr("Cannot install component %1. Component is not checkable, meaning you "
                        "have to select one of the subcomponents.\n").arg(name));
                }
            } else if (component->isInstalled()) {
                errorMessage.append(tr("Component %1 already installed\n").arg(name));
            } else {
                model->setData(idx, Qt::Checked, Qt::CheckStateRole);
                installComponentsFound = true;
            }
        } else {
            auto isDescendantOfVirtual = [&]() {
                Component *trace = component;
                forever {
                    trace = trace->parentComponent();
                    if (!trace) {
                        // We already checked the root component if there is no parent
                        return false;
                    } else if (trace->isVirtual()) {
                        errorMessage.append(tr("Cannot install %1. Component is a descendant "
                            "of a virtual component %2.\n").arg(name, trace->name()));
                        return true;
                    }
                }
            };
            // idx is invalid and component valid when we have invisible virtual component
            if (component->isVirtual())
                errorMessage.append(tr("Cannot install %1. Component is virtual.\n").arg(name));
            else if (!isDescendantOfVirtual())
                errorMessage.append(tr("Cannot install %1. Component not found.\n").arg(name));
        }
    }
    if (!installComponentsFound)
        setCanceled();

    return installComponentsFound;
}

/*!
    Lists installed packages without GUI. List of packages can be filtered with \a regexp.
*/
void PackageManagerCore::listInstalledPackages(const QString &regexp)
{
    setPackageViewer();
    LocalPackagesHash installedPackages = this->localInstalledPackages();

    if (!regexp.isEmpty()) {
        qCDebug(QInstaller::lcInstallerInstallLog)
            << "Searching packages with regular expression:" << regexp;
    }
    const QRegularExpression re(regexp);

    const QStringList &keys = installedPackages.keys();
    QList<LocalPackage> packages;
    foreach (const QString &key, keys) {
        KDUpdater::LocalPackage package = installedPackages.value(key);
        if (re.match(package.name).hasMatch())
            packages.append(package);
    }
    LoggingHandler::instance().printLocalPackageInformation(packages);
}

/*!
    Updates the selected components \a componentsToUpdate without GUI.
    If essential components are found, then only those will be updated.
    Returns PackageManagerCore installation status.
*/
PackageManagerCore::Status PackageManagerCore::updateComponentsSilently(const QStringList &componentsToUpdate)
{
    if (d->runningProcessesFound())
        throw Error(tr("Running processes found."));
    setUpdater();

    ComponentModel *model = updaterComponentModel();

    fetchRemotePackagesTree();
    // List contains components containing update, if essential found contains only essential component
    const QList<QInstaller::Component*> componentList = componentsMarkedForInstallation();

    if (componentList.count() ==  0) {
        qCDebug(QInstaller::lcInstallerInstallLog) << "No updates available.";
        setCanceled();
    } else {
        // Check if essential components are available (essential components are disabled).
        // If essential components are found, update first essential updates,
        // restart installer and install rest of the updates.
        bool essentialUpdatesFound = false;
        foreach (Component *component, componentList) {
            if ((component->value(scEssential, scFalse).toLower() == scTrue)
                || component->isForcedUpdate())
                essentialUpdatesFound = true;
        }
        if (!essentialUpdatesFound) {
            const bool userSelectedComponents = !componentsToUpdate.isEmpty();
            QList<Component*> componentsToBeUpdated;
            //Mark components to be updated
            foreach (Component *comp, componentList) {
                const QModelIndex &idx = model->indexFromComponentName(comp->treeName());
                if (!userSelectedComponents) { // No components given, update all
                    model->setData(idx, Qt::Checked, Qt::CheckStateRole);
                } else {
                    //Collect the componets to list which we want to update
                    foreach (const QString &name, componentsToUpdate) {
                        if (comp->name() == name)
                            componentsToBeUpdated.append(comp);
                        else
                            model->setData(idx, Qt::Unchecked, Qt::CheckStateRole);
                    }
                }
            }
            // No updates for selected components, do not run updater
            if (userSelectedComponents && componentsToBeUpdated.isEmpty()) {
                qCDebug(QInstaller::lcInstallerInstallLog)
                    << "No updates available for selected components.";
                return PackageManagerCore::Canceled;
            }
            foreach (Component *componentToUpdate, componentsToBeUpdated) {
                const QModelIndex &idx = model->indexFromComponentName(componentToUpdate->treeName());
                model->setData(idx, Qt::Checked, Qt::CheckStateRole);
            }
        }

        if (!d->calculateComponentsAndRun())
            return status();

        if (essentialUpdatesFound) {
            qCDebug(QInstaller::lcInstallerInstallLog) << "Essential components updated successfully."
                " Please restart maintenancetool to update other components.";
        } else {
            qCDebug(QInstaller::lcInstallerInstallLog) << "Components updated successfully.";
        }
    }
    return status();
}

/*!
    Saves temporarily current operations for installer usage. This is needed
    for unit tests when several commands (for example install and uninstall)
    are performed with the same installer instance.
*/
void PackageManagerCore::commitSessionOperations()
{
    d->commitSessionOperations();
}

/*!
 * Clears all previously added licenses.
 */
void PackageManagerCore::clearLicenses()
{
    d->m_licenseItems.clear();
}

/*!
 * Returns licenses hash which can be sorted by priority.
 */
QHash<QString, QMap<QString, QString>> PackageManagerCore::sortedLicenses()
{
    QHash<QString, QMap<QString, QString>> priorityHash;
    for (QString licenseName : d->m_licenseItems.keys()) {
        QMap<QString, QString> licenses;
        QString priority = d->m_licenseItems.value(licenseName).value(QLatin1String("priority")).toString();
        licenses = priorityHash.value(priority);
        licenses.insert(licenseName, d->m_licenseItems.value(licenseName).value(QLatin1String("content")).toString());
        priorityHash.insert(priority, licenses);
    }
    return priorityHash;
}

/*!
 * Adds new set of \a licenses. If a license with the key already exists, it is not added again.
 */
void PackageManagerCore::addLicenseItem(const QHash<QString, QVariantMap> &licenses)
{
    for (QHash<QString, QVariantMap>::const_iterator it = licenses.begin();
        it != licenses.end(); ++it) {
            if (!d->m_licenseItems.contains(it.key()))
                d->m_licenseItems.insert(it.key(), it.value());
    }
}

/*!
    Uninstalls the selected components \a components without GUI.
    Returns PackageManagerCore installation status.
*/
PackageManagerCore::Status PackageManagerCore::uninstallComponentsSilently(const QStringList& components)
{
    if (d->runningProcessesFound())
        throw Error(tr("Running processes found."));

    if (components.isEmpty()) {
        qCDebug(QInstaller::lcInstallerInstallLog) << "No components selected for uninstallation.";
        return PackageManagerCore::Canceled;
    }

    ComponentModel *model = defaultComponentModel();
    fetchLocalPackagesTree();

    bool uninstallComponentFound = false;

    foreach (const QString &componentName, components){
        Component *component = componentByName(componentName);

        if (component) {
            const QModelIndex &idx = model->indexFromComponentName(component->treeName());
            if (componentUninstallableFromCommandLine(component->name())) {
                model->setData(idx, Qt::Unchecked, Qt::CheckStateRole);
                uninstallComponentFound = true;
            }
        } else {
            qCWarning(QInstaller::lcInstallerInstallLog).noquote().nospace() << "Cannot uninstall component " << componentName <<". Component not found in install tree.";
        }
    }

    if (uninstallComponentFound) {
        if (d->calculateComponentsAndRun())
            qCDebug(QInstaller::lcInstallerInstallLog) << "Components uninstalled successfully";
    }
    return status();
}

/*!
    Uninstalls all installed components without GUI and removes
    the program directory. Returns PackageManagerCore installation status.
*/
PackageManagerCore::Status PackageManagerCore::removeInstallationSilently()
{
    setCompleteUninstallation(true);
    if (d->runningProcessesFound())
        throw Error(tr("Running processes found."));

    qCDebug(QInstaller::lcInstallerInstallLog) << "Complete uninstallation was chosen.";
    if (!(d->m_autoConfirmCommand || d->askUserConfirmCommand())) {
        qCDebug(QInstaller::lcInstallerInstallLog) << "Uninstallation aborted.";
        return status();
    }
    if (run())
        return PackageManagerCore::Success;
    else
        return PackageManagerCore::Failure;
}

/*!
    Creates an offline installer from selected \a componentsToAdd without displaying
    a user interface. Virtual components cannot be selected unless made visible with
    --show-virtual-components as in installation. AutoDependOn nor non-checkable components
    cannot be selected directly. Returns \c PackageManagerCore::Status.
*/
PackageManagerCore::Status PackageManagerCore::createOfflineInstaller(const QStringList &componentsToAdd)
{
    setOfflineGenerator();
    // init default model before fetching remote packages tree
    ComponentModel *model = defaultComponentModel();
    Q_UNUSED(model);
    if (!fetchRemotePackagesTree())
        return status();

    QString errorMessage;
    if (checkComponentsForInstallation(componentsToAdd, errorMessage)) {
        if (d->calculateComponentsAndRun()) {
            qCDebug(QInstaller::lcInstallerInstallLog)
                << "Created installer to:" << offlineBinaryName();
        }
    } else {
        qCDebug(QInstaller::lcInstallerInstallLog).noquote().nospace() << errorMessage;
    }
    return status();
}

/*!
    Installs the selected components \a components without displaying a user
    interface. Virtual components cannot be installed unless made visible with
    --show-virtual-components. AutoDependOn nor non-checkable components cannot
    be installed directly. Returns PackageManagerCore installation status.
*/
PackageManagerCore::Status PackageManagerCore::installSelectedComponentsSilently(const QStringList& components)
{
    if (!isInstaller()) {
        // Check if there are processes running in the install if maintenancetool is used.
        if (d->runningProcessesFound())
            throw Error(tr("Running processes found."));
        setPackageManager();

        //Check that packages are not already installed
        const LocalPackagesHash installedPackages = this->localInstalledPackages();
        QStringList helperStrList;
        helperStrList << components << installedPackages.keys();
        helperStrList.removeDuplicates();
        if (helperStrList.count() == installedPackages.count()) {
            qCDebug(QInstaller::lcInstallerInstallLog) << "Components already installed.";
            return PackageManagerCore::Canceled;
        }
    }

    // init default model before fetching remote packages tree
    ComponentModel *model = defaultComponentModel();
    Q_UNUSED(model);
    if (!fetchRemotePackagesTree())
        return status();

    QString errorMessage;
    if (checkComponentsForInstallation(components, errorMessage)) {
        if (d->calculateComponentsAndRun())
            qCDebug(QInstaller::lcInstallerInstallLog) << "Components installed successfully";
    } else {
        qCDebug(QInstaller::lcInstallerInstallLog).noquote().nospace() << errorMessage;
    }
    return status();
}

/*!
    Installs components that are checked by default, i.e. those that are set
    with <Default> or <ForcedInstallation> and their respective dependencies
    without GUI.
    Returns PackageManagerCore installation status.
*/
PackageManagerCore::Status PackageManagerCore::installDefaultComponentsSilently()
{
    d->m_defaultInstall = true;
    ComponentModel *model = defaultComponentModel();
    fetchRemotePackagesTree();

    if (!(model->checkedState() & ComponentModel::AllUnchecked)) {
        // There are components that are checked by default, we should install them
        if (d->calculateComponentsAndRun()) {
            qCDebug(QInstaller::lcInstallerInstallLog) << "Components installed successfully.";
        }
    } else {
        qCDebug(QInstaller::lcInstallerInstallLog) << "No components available for default installation.";
        setCanceled();
    }
    return status();
}

/*!
    Returns the settings for the package manager.
*/
Settings &PackageManagerCore::settings() const
{
    return d->m_data.settings();
}

/*!
    Tries to gain admin rights. On success, it returns \c true.

    \sa {installer::gainAdminRights}{installer.gainAdminRights}
    \sa dropAdminRights()
*/
bool PackageManagerCore::gainAdminRights()
{
    if (AdminAuthorization::hasAdminRights())
        return true;

    if (isCommandLineInstance()) {
        throw Error(tr("Cannot elevate access rights while running from command line. "
                       "Please restart the application as administrator."));
    }
    RemoteClient::instance().setActive(true);
    if (!RemoteClient::instance().isActive())
        throw Error(tr("Error while elevating access rights."));
    return true;
}

/*!
    \sa {installer::dropAdminRights}{installer.dropAdminRights}
    \sa gainAdminRights()
*/
void PackageManagerCore::dropAdminRights()
{
    RemoteClient::instance().setActive(false);
}

/*!
    Sets checkAvailableSpace based on value of \a check.
*/
void PackageManagerCore::setCheckAvailableSpace(bool check)
{
    d->m_checkAvailableSpace = check;
}

/*!
    Checks available disk space if the feature is not explicitly disabled. Informative
    text about space status can be retrieved by passing \a message parameter. Returns
    \c true if there is sufficient free space on installation and temporary volumes.
*/
bool PackageManagerCore::checkAvailableSpace(QString &message) const
{
    const quint64 extraSpace = 256 * 1024 * 1024LL;
    quint64 required(requiredDiskSpace());
    quint64 tempRequired(requiredTemporaryDiskSpace());
    if (required < extraSpace) {
        required += 0.1 * required;
        tempRequired += 0.1 * tempRequired;
    } else {
        required += extraSpace;
        tempRequired += extraSpace;
    }

    quint64 repositorySize = 0;
    const bool createLocalRepository = createLocalRepositoryFromBinary();
    if (createLocalRepository && isInstaller()) {
        repositorySize = QFile(QCoreApplication::applicationFilePath()).size();
        // if we create a local repository, take that space into account as well
        required += repositorySize;
    }
    // if we create offline installer, take current executable size into account
    if (isOfflineGenerator())
        required += QFile(QCoreApplication::applicationFilePath()).size();

    qDebug() << "Installation space required:" << humanReadableSize(required) << "Temporary space "
        "required:" << humanReadableSize(tempRequired) << "Local repository size:"
        << humanReadableSize(repositorySize);

    if (d->m_checkAvailableSpace) {
        const VolumeInfo tempVolume = VolumeInfo::fromPath(QDir::tempPath());
        const VolumeInfo targetVolume = VolumeInfo::fromPath(value(scTargetDir));

        const quint64 tempVolumeAvailableSize = tempVolume.availableSize();
        const quint64 installVolumeAvailableSize = targetVolume.availableSize();

        // at the moment there is no better way to check this
        if (targetVolume.size() == 0 && installVolumeAvailableSize == 0) {
            qDebug().nospace() << "Cannot determine available space on device. "
                                  "Volume descriptor: " << targetVolume.volumeDescriptor()
                               << ", Mount path: " << targetVolume.mountPath() << ". Continue silently.";
            return true;
        }

        const bool tempOnSameVolume = (targetVolume == tempVolume);
        if (tempOnSameVolume) {
            qDebug() << "Tmp and install directories are on the same volume. Volume mount point:"
                << targetVolume.mountPath() << "Free space available:"
                << humanReadableSize(installVolumeAvailableSize);
        } else {
            qDebug() << "Tmp is on a different volume than the installation directory. Tmp volume mount point:"
                << tempVolume.mountPath() << "Free space available:"
                << humanReadableSize(tempVolumeAvailableSize) << "Install volume mount point:"
                << targetVolume.mountPath() << "Free space available:"
                << humanReadableSize(installVolumeAvailableSize);
        }

        if (tempOnSameVolume && (installVolumeAvailableSize <= (required + tempRequired))) {
            message = tr("Not enough disk space to store temporary files and the "
                "installation. %1 are available, while the minimum required is %2.").arg(
                humanReadableSize(installVolumeAvailableSize), humanReadableSize(required + tempRequired));
            return false;
        }

        if (installVolumeAvailableSize < required) {
            message = tr("Not enough disk space to store all selected components! %1 are "
                "available, while the minimum required is %2.").arg(humanReadableSize(installVolumeAvailableSize),
                humanReadableSize(required));
            return false;
        }

        if (tempVolumeAvailableSize < tempRequired) {
            message = tr("Not enough disk space to store temporary files! %1 are available, "
                "while the minimum required is %2.").arg(humanReadableSize(tempVolumeAvailableSize),
                humanReadableSize(tempRequired));
            return false;
        }

        if (installVolumeAvailableSize - required < 0.01 * targetVolume.size()) {
            // warn for less than 1% of the volume's space being free
            message = tr("The volume you selected for installation seems to have sufficient space for "
                "installation, but there will be less than 1% of the volume's space available afterwards.");
        } else if (installVolumeAvailableSize - required < 100 * 1024 * 1024LL) {
            // warn for less than 100MB being free
            message = tr("The volume you selected for installation seems to have sufficient "
                "space for installation, but there will be less than 100 MB available afterwards.");
        }
#ifdef Q_OS_WIN
        if (isOfflineGenerator() && (required > UINT_MAX)) {
            message = tr("The estimated installer size %1 would exceed the supported executable "
                "size limit of %2. The application may not be able to run.")
                .arg(humanReadableSize(required), humanReadableSize(UINT_MAX));
        }
#endif
    }
    message = QString::fromLatin1("%1 %2").arg(message, tr("Installation will use %1 of disk space.")
        .arg(humanReadableSize(requiredDiskSpace()))).simplified();

    return true;
}

/*!
    Returns \c true if a process with \a name is running. On Windows, the comparison
    is case-insensitive.

    \sa {installer::isProcessRunning}{installer.isProcessRunning}
*/
bool PackageManagerCore::isProcessRunning(const QString &name) const
{
    return PackageManagerCorePrivate::isProcessRunning(name, runningProcesses());
}

/*!
    Returns \c true if a process with \a absoluteFilePath could be killed or is
    not running.

    \note This is implemented in a semi blocking way (to keep the main thread to paint the UI).

    \sa {installer::killProcess}{installer.killProcess}
*/
bool PackageManagerCore::killProcess(const QString &absoluteFilePath) const
{
    QString normalizedPath = replaceVariables(absoluteFilePath);
    normalizedPath = QDir::cleanPath(normalizedPath.replace(QLatin1Char('\\'), QLatin1Char('/')));

    QList<ProcessInfo> allProcesses = runningProcesses();
    foreach (const ProcessInfo &process, allProcesses) {
        QString processPath = process.name;
        processPath =  QDir::cleanPath(processPath.replace(QLatin1Char('\\'), QLatin1Char('/')));

        if (processPath == normalizedPath) {
            qCDebug(QInstaller::lcInstallerInstallLog).nospace() << "try to kill process " << process.name
                << " (" << process.id << ")";

            //to keep the ui responsible use QtConcurrent::run
            QFutureWatcher<bool> futureWatcher;
            const QFuture<bool> future = QtConcurrent::run(KDUpdater::killProcess, process, 30000);

            QEventLoop loop;
            connect(&futureWatcher, &QFutureWatcher<bool>::finished,
                    &loop, &QEventLoop::quit, Qt::QueuedConnection);
            futureWatcher.setFuture(future);

            if (!future.isFinished())
                loop.exec();

            qCDebug(QInstaller::lcInstallerInstallLog) << process.name << "killed!";
            return future.result();
        }
    }
    return true;
}

/*!
    Sets additional \a processes that can run when
    updating with the maintenance tool.

    \sa {installer::setAllowedRunningProcesses}{installer.setAllowedRunningProcesses}
*/
void PackageManagerCore::setAllowedRunningProcesses(const QStringList &processes)
{
    d->m_allowedRunningProcesses = processes;
}

/*!
    Returns processes that are allowed to run when
    updating with the maintenance tool.

    \sa {installer::allowedRunningProcesses}{installer.allowedRunningProcesses}
*/
QStringList PackageManagerCore::allowedRunningProcesses() const
{
    return d->m_allowedRunningProcesses;
}

Q_INVOKABLE void QInstaller::PackageManagerCore::forceQuit()
{
    exit(EXIT_SUCCESS);
}

/*!
    Makes sure the installer runs from a local drive. Otherwise the user will get an
    appropriate error message.

    \note This only works on Windows.

    \sa {installer::setDependsOnLocalInstallerBinary}{installer.setDependsOnLocalInstallerBinary}
    \sa localInstallerBinaryUsed()
*/

void PackageManagerCore::setDependsOnLocalInstallerBinary()
{
    d->m_dependsOnLocalInstallerBinary = true;
}

/*!
    Returns \c false if the installer is run on Windows, and the installer has been started from
    a remote file system drive. Otherwise returns \c true.

    \sa {installer::localInstallerBinaryUsed}{installer.localInstallerBinaryUsed}
    \sa setDependsOnLocalInstallerBinary()
*/
bool PackageManagerCore::localInstallerBinaryUsed()
{
#ifdef Q_OS_WIN
    return KDUpdater::pathIsOnLocalDevice(qApp->applicationFilePath());
#endif
    return true;
}

/*!
    Starts the program \a program with the arguments \a arguments in a
    new process and waits for it to finish.

    \a stdIn is sent as standard input to the application.

    \a stdInCodec is the name of the codec to use for converting the input string
    into bytes to write to the standard input of the application.

    \a stdOutCodec is the name of the codec to use for converting data written by the
    application to standard output into a string.

    Returns an empty array if the program could not be executed, otherwise
    the output of command as the first item, and the return code as the second.

    \note On Unix, the output is just the output to stdout, not to stderr.

    \sa {installer::execute}{installer.execute}
    \sa executeDetached()
*/
QList<QVariant> PackageManagerCore::execute(const QString &program, const QStringList &arguments,
    const QString &stdIn, const QString &stdInCodec, const QString &stdOutCodec) const
{
    QProcessWrapper process;

    QString adjustedProgram = replaceVariables(program);
    QStringList adjustedArguments;
    foreach (const QString &argument, arguments)
        adjustedArguments.append(replaceVariables(argument));
    QString adjustedStdIn = replaceVariables(stdIn);

    process.start(adjustedProgram, adjustedArguments,
        adjustedStdIn.isNull() ? QIODevice::ReadOnly : QIODevice::ReadWrite);

    if (!process.waitForStarted())
        return QList< QVariant >();

    if (!adjustedStdIn.isNull()) {
        QTextCodec *codec = QTextCodec::codecForName(qPrintable(stdInCodec));
        if (!codec)
            return QList<QVariant>();

        QTextEncoder encoder(codec);
        process.write(encoder.fromUnicode(adjustedStdIn));
        process.closeWriteChannel();
    }

    process.waitForFinished(-1);

    QTextCodec *codec = QTextCodec::codecForName(qPrintable(stdOutCodec));
    if (!codec)
        return QList<QVariant>();
    return QList<QVariant>()
            << QTextDecoder(codec).toUnicode(process.readAllStandardOutput())
            << process.exitCode();
}

/*!
    Starts the program \a program with the arguments \a arguments in a
    new process, and detaches from it. Returns \c true on success;
    otherwise returns \c false. If the installer exits, the
    detached process will continue to live.

    \note Arguments that contain spaces are not passed to the
    process as separate arguments.

    \b{Unix:} The started process will run in its own session and act
    like a daemon.

    \b{Windows:} Arguments that contain spaces are wrapped in quotes.
    The started process will run as a regular standalone process.

    The process will be started in the directory \a workingDirectory.

    \sa {installer::executeDetached}{installer.executeDetached}
*/

bool PackageManagerCore::executeDetached(const QString &program, const QStringList &arguments,
    const QString &workingDirectory) const
{
    QString adjustedProgram = replaceVariables(program);
    QStringList adjustedArguments;
    QString adjustedWorkingDir = replaceVariables(workingDirectory);
    foreach (const QString &argument, arguments)
        adjustedArguments.append(replaceVariables(argument));
    qCDebug(QInstaller::lcInstallerInstallLog) << "run application as detached process:" << adjustedProgram
        << adjustedArguments << adjustedWorkingDir;
    if (workingDirectory.isEmpty())
        return QProcess::startDetached(adjustedProgram, adjustedArguments);
    else
        return QProcess::startDetached(adjustedProgram, adjustedArguments, adjustedWorkingDir);
}


/*!
    Returns the content of the environment variable \a name. An empty string is returned if the
    environment variable is not set.

    \sa {installer::environmentVariable}{installer.environmentVariable}
*/
QString PackageManagerCore::environmentVariable(const QString &name) const
{
    if (name.isEmpty())
        return QString();

#ifdef Q_OS_WIN
    static TCHAR buffer[32767];
    DWORD size = GetEnvironmentVariable(LPCWSTR(name.utf16()), buffer, 32767);
    QString value = QString::fromUtf16((const unsigned short *) buffer, size);

    if (value.isEmpty()) {
        static QLatin1String userEnvironmentRegistryPath("HKEY_CURRENT_USER\\Environment");
        value = QSettings(userEnvironmentRegistryPath, QSettings::NativeFormat).value(name).toString();
        if (value.isEmpty()) {
            static QLatin1String systemEnvironmentRegistryPath("HKEY_LOCAL_MACHINE\\SYSTEM\\"
                "CurrentControlSet\\Control\\Session Manager\\Environment");
            value = QSettings(systemEnvironmentRegistryPath, QSettings::NativeFormat).value(name).toString();
        }
    }
    return value;
#else
    return QString::fromUtf8(qgetenv(name.toLatin1()));
#endif
}

/*!
    Returns \c true if the operation specified by \a name exists.
*/
bool PackageManagerCore::operationExists(const QString &name)
{
    return KDUpdater::UpdateOperationFactory::instance().containsProduct(name);
}

/*!
    Performs the operation \a name with \a arguments.

    Returns \c false if the operation cannot be created or executed.

    \note The operation is performed threaded. It is not advised to call
    this function after installation finished signals.

    \sa {installer::performOperation}{installer.performOperation}
*/
bool PackageManagerCore::performOperation(const QString &name, const QStringList &arguments)
{
    QScopedPointer<Operation> op(KDUpdater::UpdateOperationFactory::instance().create(name, this));
    if (!op.data())
        return false;

    op->setArguments(replaceVariables(arguments));
    op->backup();
    if (!PackageManagerCorePrivate::performOperationThreaded(op.data())) {
        PackageManagerCorePrivate::performOperationThreaded(op.data(), Operation::Undo);
        return false;
    }
    return true;
}

bool QInstaller::PackageManagerCore::extract7z(const QString& filePath, const QString& outputDir) {
    try {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadWrite) || !file.exists()) {
            return false;
        }

        Lib7z::extractArchive(&file, outputDir);
        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

QString QInstaller::PackageManagerCore::getFirstFilePath(const QString& path, const QString& ext) const {
    QDir dir(path);

    auto list = dir.entryList(QStringList() << ext, QDir::Files);

    return list.size() > 0 ? dir.filePath(list.at(0)) : QString();
}

/*!
    Returns \c true when \a version matches the \a requirement.
    \a requirement can be a fixed version number or it can be prefixed by the comparators '>', '>=',
    '<', '<=' and '='.

    \sa {installer::versionMatches}{installer.versionMatches}
*/
bool PackageManagerCore::versionMatches(const QString &version, const QString &requirement)
{
    QRegExp compEx(QLatin1String("([<=>]+)(.*)"));
    const QString comparator = compEx.exactMatch(requirement) ? compEx.cap(1) : QLatin1String("=");
    const QString ver = compEx.exactMatch(requirement) ? compEx.cap(2) : requirement;

    const bool allowEqual = comparator.contains(QLatin1Char('='));
    const bool allowLess = comparator.contains(QLatin1Char('<'));
    const bool allowMore = comparator.contains(QLatin1Char('>'));

    if (allowEqual && version == ver)
        return true;

    if (allowLess && KDUpdater::compareVersion(ver, version) > 0)
        return true;

    if (allowMore && KDUpdater::compareVersion(ver, version) < 0)
        return true;

    return false;
}

/*!
    Finds a library named \a name in \a paths.
    If \a paths is empty, it gets filled with platform dependent default paths.
    The resulting path is returned.

    This method can be used by scripts to check external dependencies.

    \sa {installer::findLibrary}{installer.findLibrary}
    \sa findPath()
*/
QString PackageManagerCore::findLibrary(const QString &name, const QStringList &paths)
{
    QStringList findPaths = paths;
#if defined(Q_OS_WIN)
    return findPath(QString::fromLatin1("%1.lib").arg(name), findPaths);
#else
#if defined(Q_OS_MACOS)
    if (findPaths.isEmpty()) {
        findPaths.push_back(QLatin1String("/lib"));
        findPaths.push_back(QLatin1String("/usr/lib"));
        findPaths.push_back(QLatin1String("/usr/local/lib"));
        findPaths.push_back(QLatin1String("/opt/local/lib"));
    }
    const QString dynamic = findPath(QString::fromLatin1("lib%1.dylib").arg(name), findPaths);
#else
    if (findPaths.isEmpty()) {
        findPaths.push_back(QLatin1String("/lib"));
        findPaths.push_back(QLatin1String("/usr/lib"));
        findPaths.push_back(QLatin1String("/usr/local/lib"));
        findPaths.push_back(QLatin1String("/lib64"));
        findPaths.push_back(QLatin1String("/usr/lib64"));
        findPaths.push_back(QLatin1String("/usr/local/lib64"));
    }
    const QString dynamic = findPath(QString::fromLatin1("lib%1.so*").arg(name), findPaths);
#endif
    if (!dynamic.isEmpty())
        return dynamic;
    return findPath(QString::fromLatin1("lib%1.a").arg(name), findPaths);
#endif
}

/*!
    Tries to find the file name \a name in one of the paths specified by \a paths.
    The resulting path is returned.

    This method can be used by scripts to check external dependencies.

    \sa {installer::findPath}{installer.findPath}
    \sa findLibrary()
*/
QString PackageManagerCore::findPath(const QString &name, const QStringList &paths)
{
    foreach (const QString &path, paths) {
        const QDir dir(path);
        const QStringList entries = dir.entryList(QStringList() << name, QDir::Files | QDir::Hidden);
        if (entries.isEmpty())
            continue;

        return dir.absoluteFilePath(entries.first());
    }
    return QString();
}

/*!
    Sets the \c installerbase binary located at \a path to use when writing the
    maintenance tool. Set the path if an update to the binary is available.

    If the path is not set, the executable segment of the running installer or uninstaller
    will be used.

    \sa {installer::setInstallerBaseBinary}{installer.setInstallerBaseBinary}
*/
void PackageManagerCore::setInstallerBaseBinary(const QString &path)
{
    d->m_installerBaseBinaryUnreplaced = path;
}

/*!
    Sets the \c installerbase binary located at \a path to use when writing the
    offline installer. Setting this makes it possible to run the offline generator
    in cases where we are not running a real installer, i.e. when executing autotests.

    For normal runs, the executable segment of the running installer will be used.
*/
void PackageManagerCore::setOfflineBaseBinary(const QString &path)
{
    d->m_offlineBaseBinaryUnreplaced = path;
}

/*!
    Adds the resource collection in \a rcPath to the list of resource files
    to be included into the generated offline installer binary.
*/
void PackageManagerCore::addResourcesForOfflineGeneration(const QString &rcPath)
{
    d->m_offlineGeneratorResourceCollections.append(rcPath);
}

/*!
    Returns the installer value for \a key. If \a key is not known to the system, \a defaultValue is
    returned. Additionally, on Windows, \a key can be a registry key.

    \sa {installer::value}{installer.value}
    \sa setValue(), containsValue(), valueChanged()
*/
QString PackageManagerCore::value(const QString &key, const QString &defaultValue) const
{
    return d->m_data.value(key, defaultValue).toString();
}

/*!
    Returns the installer value for \a key. If \a key is not known to the system, \a defaultValue is
    returned. Additionally, on Windows, \a key can be a registry key.

    \sa {installer::values}{installer.values}
    \sa value()
*/
QStringList PackageManagerCore::values(const QString &key, const QStringList &defaultValue) const
{
    return d->m_data.value(key, defaultValue).toStringList();
}

/*!
    Returns the installer key for \a value. If \a value is not known, empty string is
    returned.

    \sa {installer::key}{installer.key}
*/
QString PackageManagerCore::key(const QString &value) const
{
    return d->m_data.key(value);
}

/*!
    Sets the installer value for \a key to \a value.

    \sa {installer::setValue}{installer.setValue}
    \sa value(), containsValue(), valueChanged()
*/
void PackageManagerCore::setValue(const QString &key, const QString &value)
{
    const QString normalizedValue = replaceVariables(value);
    if (d->m_data.setValue(key, normalizedValue))
        emit valueChanged(key, normalizedValue);
}

/*!
    Returns \c true if the installer contains a value for \a key.

    \sa {installer::containsValue}{installer.containsValue}
    \sa value(), setValue(), valueChanged()
*/
bool PackageManagerCore::containsValue(const QString &key) const
{
    return d->m_data.contains(key);
}

/*!
    Returns \c true if the package manager displays detailed information.
*/
bool PackageManagerCore::isVerbose() const
{
    return LoggingHandler::instance().isVerbose();
}

/*!
    Determines that the package manager displays detailed information if
    \a on is \c true. Calling setVerbose() more than once increases verbosity.
*/
void PackageManagerCore::setVerbose(bool on)
{
    LoggingHandler::instance().setVerbose(on);
}

PackageManagerCore::Status PackageManagerCore::status() const
{
    return PackageManagerCore::Status(d->m_status);
}

/*!
    Returns a human-readable description of the last error that occurred.
*/
QString PackageManagerCore::error() const
{
    return d->m_error;
}

/*!
    Returns \c true if at least one complete installation or update was
    successful, even if the user cancelled the latest installation process.
*/
bool PackageManagerCore::finishedWithSuccess() const
{
    return d->m_status == PackageManagerCore::Success || d->m_needToWriteMaintenanceTool;
}

/*!
    \sa {installer::interrupt}{installer.interrupt}
    \sa installationInterrupted()
 */
void PackageManagerCore::interrupt()
{
    setCanceled();
    emit installationInterrupted();
}

/*!
    \sa {installer::setCanceled}{installer.setCanceled}
 */
void PackageManagerCore::setCanceled()
{
    if (!d->m_repoFetched)
        cancelMetaInfoJob();
    d->setStatus(PackageManagerCore::Canceled);
}

/*!
    Replaces all variables within \a str by their respective values and returns the result.
*/
QString PackageManagerCore::replaceVariables(const QString &str) const
{
    return d->replaceVariables(str);
}

/*!
    \overload
    Replaces all variables in any instance of \a str by their respective values
    and returns the results.
*/
QStringList PackageManagerCore::replaceVariables(const QStringList &str) const
{
    QStringList result;
    foreach (const QString &s, str)
        result.append(d->replaceVariables(s));

    return result;
}

/*!
    \overload
    Replaces all variables within \a ba by their respective values and returns the result.
*/
QByteArray PackageManagerCore::replaceVariables(const QByteArray &ba) const
{
    return d->replaceVariables(ba);
}

/*!
    Returns the path to the installer binary.
*/
QString PackageManagerCore::installerBinaryPath() const
{
    return d->installerBinaryPath();
}

/*!
    Sets the \a name for the generated offline binary.
*/
void PackageManagerCore::setOfflineBinaryName(const QString &name)
{
    setValue(scOfflineBinaryName, name);
}

/*!
    Returns the path set for the generated offline binary.
*/
QString PackageManagerCore::offlineBinaryName() const
{
    return d->offlineBinaryName();
}

/*!
    \sa {installer::setInstaller}{installer.setInstaller}
    \sa isInstaller(), setUpdater(), setPackageManager()
*/
void PackageManagerCore::setInstaller()
{
    d->m_magicBinaryMarker = BinaryContent::MagicInstallerMarker;
    emit installerBinaryMarkerChanged(d->m_magicBinaryMarker);
}

/*!
    Returns \c true if running as installer.

    \sa {installer::isInstaller}{installer.isInstaller}
    \sa isUninstaller(), isUpdater(), isPackageManager()
*/
bool PackageManagerCore::isInstaller() const
{
    return d->isInstaller();
}

/*!
    Returns \c true if this is an offline-only installer.

    \sa {installer::isOfflineOnly}{installer.isOfflineOnly}
*/
bool PackageManagerCore::isOfflineOnly() const
{
    return d->isOfflineOnly();
}

/*!
    \sa {installer::setUninstaller}{installer.setUninstaller}
    \sa isUninstaller(), setUpdater(), setPackageManager()
*/
void PackageManagerCore::setUninstaller()
{
    d->m_magicBinaryMarker = BinaryContent::MagicUninstallerMarker;
    emit installerBinaryMarkerChanged(d->m_magicBinaryMarker);
}

/*!
    Returns \c true if running as uninstaller.

    \sa {installer::isUninstaller}{installer.isUninstaller}
    \sa setUninstaller(), isInstaller(), isUpdater(), isPackageManager()
*/
bool PackageManagerCore::isUninstaller() const
{
    return d->isUninstaller();
}

Q_INVOKABLE void QInstaller::PackageManagerCore::setReinstaller()
{
    d->m_magicBinaryMarker = BinaryContent::MagicReinstallerMarker;
    emit installerBinaryMarkerChanged(d->m_magicBinaryMarker);
}

Q_INVOKABLE bool QInstaller::PackageManagerCore::isReinstaller() const
{
    return d->isReinstaller();
}

/*!
    \sa {installer::setUpdater}{installer.setUpdater}
    \sa isUpdater(), setUninstaller(), setPackageManager()
*/
void PackageManagerCore::setUpdater()
{
    d->m_magicBinaryMarker = BinaryContent::MagicUpdaterMarker;
    emit installerBinaryMarkerChanged(d->m_magicBinaryMarker);
}

/*!
    Returns \c true if running as updater.

    \sa {installer::isUpdater}{installer.isUpdater}
    \sa setUpdater(), isInstaller(), isUninstaller(), isPackageManager()
*/
bool PackageManagerCore::isUpdater() const
{
    return d->isUpdater();
}

/*!
    \sa {installer::setPackageManager}{installer.setPackageManager}
*/
void PackageManagerCore::setPackageManager()
{
    d->m_magicBinaryMarker = BinaryContent::MagicPackageManagerMarker;
    emit installerBinaryMarkerChanged(d->m_magicBinaryMarker);
}


/*!
    Returns \c true if running as the package manager.

    \sa {installer::isPackageManager}{installer.isPackageManager}
    \sa setPackageManager(), isInstaller(), isUninstaller(), isUpdater()
*/
bool PackageManagerCore::isPackageManager() const
{
    return d->isPackageManager();
}

/*!
    Sets current installer to be offline generator.
*/
void PackageManagerCore::setOfflineGenerator()
{
    d->m_magicMarkerSupplement = BinaryContent::OfflineGenerator;
}

/*!
    Returns \c true if current installer is executed as offline generator.

    \sa {installer::isOfflineGenerator}{installer.isOfflineGenerator}
*/
bool PackageManagerCore::isOfflineGenerator() const
{
    return d->isOfflineGenerator();
}

/*!
    Sets the current installer as the package viewer.
*/
void PackageManagerCore::setPackageViewer()
{
    d->m_magicMarkerSupplement = BinaryContent::PackageViewer;
}

/*!
    Returns \c true if the current installer is executed as package viewer.

    \sa {installer::isPackageViewer}{installer.isPackageViewer}
*/
bool PackageManagerCore::isPackageViewer() const
{
    return d->isPackageViewer();
}

/*!
    Sets the installer magic binary marker based on \a magicMarker and
    userSetBinaryMarker to \c true.
*/
void PackageManagerCore::setUserSetBinaryMarker(qint64 magicMarker)
{
    d->m_magicBinaryMarker = magicMarker;
    d->m_userSetBinaryMarker = true;
    emit installerBinaryMarkerChanged(d->m_magicBinaryMarker);
}

/*!
    Returns \c true if the magic binary marker has been set by user,
    for example from a command line argument.

    \sa {installer::isUserSetBinaryMarker}{installer.isUserSetBinaryMarker}
*/
bool PackageManagerCore::isUserSetBinaryMarker() const
{
    return d->m_userSetBinaryMarker;
}

/*!
    Set to use command line instance based on \a commandLineInstance.
*/
void PackageManagerCore::setCommandLineInstance(bool commandLineInstance)
{
    d->m_commandLineInstance = commandLineInstance;
}

/*!
    Returns \c true if running as command line instance.

    \sa {installer::isCommandLineInstance}{installer.isCommandLineInstance}
*/
bool PackageManagerCore::isCommandLineInstance() const
{
    return d->m_commandLineInstance;
}

/*!
    Returns \c true if installation is performed with default components.

    \sa {installer::isCommandLineDefaultInstall}{installer.isCommandLineDefaultInstall}
*/
bool PackageManagerCore::isCommandLineDefaultInstall() const
{
    return d->m_defaultInstall;
}
/*!
    Returns \c true if it is a package manager or an updater.
*/
bool PackageManagerCore::isMaintainer() const
{
    return isPackageManager() || isUpdater() || isReinstaller();
}

/*!
    Runs the installer. Returns \c true on success, \c false otherwise.

    \sa {installer::runInstaller}{installer.runInstaller}
*/
bool PackageManagerCore::runInstaller()
{
    return d->runInstaller();
}

bool PackageManagerCore::runReinstaller()
{
    return d->runReinstaller();
}

/*!
    Runs the uninstaller. Returns \c true on success, \c false otherwise.

    \sa {installer::runUninstaller}{installer.runUninstaller}
*/
bool PackageManagerCore::runUninstaller()
{
    return d->runUninstaller();
}

/*!
    Runs the updater. Returns \c true on success, \c false otherwise.

    \sa {installer::runPackageUpdater}{installer.runPackageUpdater}
*/
bool PackageManagerCore::runPackageUpdater()
{
    return d->runPackageUpdater();
}

/*!
    Runs the offline generator. Returns \c true on success, \c false otherwise.

    \sa {installer::runOfflineGenerator}{installer.runOfflineGenerator}
*/
bool PackageManagerCore::runOfflineGenerator()
{
    return d->runOfflineGenerator();
}

/*!
    \sa {installer::languageChanged}{installer.languageChanged}
*/
void PackageManagerCore::languageChanged()
{
    foreach (Component *component, components(ComponentType::All))
        component->languageChanged();
}

/*!
    Runs the installer, uninstaller, updater, package manager, or offline generator depending on
    the type of this binary. Returns \c true on success, otherwise \c false.
*/
bool PackageManagerCore::run()
{
    if (isOfflineGenerator())
        return d->runOfflineGenerator();
    else if (isInstaller())
        return d->runInstaller();
    else if (isUninstaller())
        return d->runUninstaller();
    else if (isMaintainer())
        return d->runPackageUpdater();

    return false;
}

/*!
    Returns the path name of the maintenance tool binary.
*/
QString PackageManagerCore::maintenanceToolName() const
{
    return d->maintenanceToolName();
}

bool PackageManagerCore::updateComponentData(struct Data &data, Component *component)
{
    try {
        // Check if we already added the component to the available components list.
        // Component treenames and names must be unique.
        QString name = data.package->data(scTreeName).toString();
        if (name.isEmpty())
            name = data.package->data(scName).toString();
        if (data.components->contains(name)) {
            d->setStatus(Failure, tr("Cannot register component! Component with identifier %1 "
                                     "already exists.").arg(name));
            return false;
        }
        name = data.package->data(scName).toString();
        if (settings().allowUnstableComponents()) {
            // Check if there are sha checksum mismatch. Component will still show in install tree
            // but is unselectable.
            foreach (const QString packageName, d->m_metadataJob.shaMismatchPackages()) {
                if (packageName == component->name()) {
                    QString errorString = QLatin1String("SHA mismatch detected for component ") + packageName;
                    component->setUnstable(Component::UnstableError::ShaMismatch, errorString);
                }
            }
        }

        component->setUninstalled();
        const QString localPath = component->localTempPath();
        if (LoggingHandler::instance().verboseLevel() == LoggingHandler::Detailed) {
            static QString lastLocalPath;
            if (lastLocalPath != localPath)
                qCDebug(QInstaller::lcDeveloperBuild()) << "Url is:" << localPath;
            lastLocalPath = localPath;
        }


        const Repository repo = d->m_metadataJob.repositoryForDirectory(localPath);
        if (repo.isValid()) {
            component->setRepositoryUrl(repo.url());
            component->setValue(QLatin1String("username"), repo.username());
            component->setValue(QLatin1String("password"), repo.password());
        }

        // add downloadable archive from xml
        const QStringList downloadableArchives = data.package->data(scDownloadableArchives).toString()
            .split(QInstaller::commaRegExp(), QString::SkipEmptyParts);

        if (component->isFromOnlineRepository()) {
            foreach (const QString downloadableArchive, downloadableArchives)
                component->addDownloadableArchive(downloadableArchive);
        }

        const QStringList componentsToReplace = data.package->data(scReplaces).toString()
            .split(QInstaller::commaRegExp(), QString::SkipEmptyParts);

        if (!componentsToReplace.isEmpty()) {
            // Store the component (this is a component that replaces others) and all components that
            // this one will replace.
            data.replacementToExchangeables.insert(component, componentsToReplace);
        }

        if (isInstaller()) {
            // Running as installer means no component is installed, we do not need to check if the
            // replacement needs to be marked as installed, just return.
            return true;
        }

        if (data.installedPackages->contains(name)) {
            // The replacement is already installed, we can mark it as installed and skip the search for
            // a possible component to replace that might be installed (to mark the replacement as installed).
            component->setInstalled();
            component->setValue(scInstalledVersion, data.installedPackages->value(name).version);
            return true;
        }

        // The replacement is not yet installed, check all components to replace for there install state.
        foreach (const QString &componentName, componentsToReplace) {
            if (data.installedPackages->contains(componentName)) {
                // We found a replacement that is installed.
                if (isPackageManager()) {
                    // Mark the replacement component as installed as well. Only do this in package manager
                    // mode, otherwise it would not show up in the updaters component list.
                    component->setInstalled();
                    component->setValue(scInstalledVersion, data.installedPackages->value(componentName).version);
                    break;  // Break as soon as we know we found an installed component this one replaces.
                }
            }
        }
    } catch (...) {
        return false;
    }

    return true;
}

void PackageManagerCore::storeReplacedComponents(QHash<QString, Component *> &components, const struct Data &data)
{
    QHash<Component*, QStringList>::const_iterator it = data.replacementToExchangeables.constBegin();
    // remember all components that got a replacement, required for uninstall
    for (; it != data.replacementToExchangeables.constEnd(); ++it) {
        foreach (const QString &componentName, it.value()) {
            Component *componentToReplace = components.take(componentName);
            if (!componentToReplace) {
                // If a component replaces another component which is not existing in the
                // installer binary or the installed component list, just ignore it. This
                // can happen when in installer mode and probably package manager mode too.
                if (isUpdater() || isReinstaller())
                    qCWarning(QInstaller::lcDeveloperBuild) << componentName << "- Does not exist in the repositories anymore.";
                continue;
            }
            if (!d->componentsToReplace().contains(componentName)) {
                componentToReplace = new Component(this);
                componentToReplace->setValue(scName, componentName);
            } else {
                // This case can happen when in installer mode as well, a component
                // is in the installer binary and its replacement component as well.
                d->replacementDependencyComponents().append(componentToReplace);
            }
            d->componentsToReplace().insert(componentName, qMakePair(it.key(), componentToReplace));
        }
    }
}

bool PackageManagerCore::fetchAllPackages(const PackagesList &remotes, const LocalPackagesHash &locals)
{
    emit startAllComponentsReset();

    d->clearAllComponentLists();
    QHash<QString, QInstaller::Component*> components;

    Data data;
    data.components = &components;
    data.installedPackages = &locals;

    QMap<QString, QString> treeNameComponents;
    foreach (Package *const package, remotes) {
        if (d->statusCanceledOrFailed())
            return false;

        if (!ProductKeyCheck::instance()->isValidPackage(package->data(scName).toString()))
            continue;

        QScopedPointer<QInstaller::Component> component(new QInstaller::Component(this));
        data.package = package;
        component->loadDataFromPackage(*package);
        if (updateComponentData(data, component.data())) {
            // Create a list where is name and treename. Repo can contain a package with
            // a different treename of component which is already installed. We don't want
            // to move already installed local packages.
            const QString treeName = component->value(scTreeName);
            if (!treeName.isEmpty())
                treeNameComponents.insert(component->name(), treeName);
            QString name = component->treeName();
            components.insert(name, component.take());
        } else {
            return false;
        }
    }

    foreach (const QString &key, locals.keys()) {
        QScopedPointer<QInstaller::Component> component(new QInstaller::Component(this));
        component->loadDataFromPackage(locals.value(key));
        QString treeName = component->treeName();

        // 1. Component has a treename in local but not in remote
        if (!treeNameComponents.contains(component->name()) && !component->value(scTreeName).isEmpty()) {
            Component *comp = components.take(component->name());
            delete comp;
            components.insert(treeName, component.take());
        // 2. Component has different treename in local and remote, add with local treename
        } else if (treeNameComponents.contains(component->name())) {
            QString remoteTreeName = treeNameComponents.value(component->name());
            QString componentTreeName = component->value(scTreeName);
            if (remoteTreeName != componentTreeName) {
                Component *comp = components.take(treeNameComponents.value(component->name()));
                delete comp;
                components.insert(treeName, component.take());
            }
        // 3. Component has same treename in local and remote, don't add the component again.
        } else if (!components.contains(treeName)) {
            components.insert(treeName, component.take());
        }
    }

    // store all components that got a replacement
    storeReplacedComponents(components, data);

    if (!d->buildComponentTree(components, true))
        return false;

    emit finishAllComponentsReset(d->m_rootComponents);
    return true;
}

bool PackageManagerCore::fetchReinstallerPackages(const PackagesList& remotes, const LocalPackagesHash& locals) {
    emit startReinstallerComponentsReset();

    d->clearReinstallerComponentLists();
    QHash<QString, QInstaller::Component*> components;

    Data data;
    data.components = &components;
    data.installedPackages = &locals;

    setFoundEssentialUpdate(false);
    LocalPackagesHash installedPackages = locals;
    QStringList replaceMes;

    foreach(Package* const update, remotes) {
        if (d->statusCanceledOrFailed())
            return false;

        if (!ProductKeyCheck::instance()->isValidPackage(update->data(scName).toString()))
            continue;

        QScopedPointer<QInstaller::Component> component(new QInstaller::Component(this));
        data.package = update;
        component->loadDataFromPackage(*update);
        if (updateComponentData(data, component.data())) {
            // Keep a reference so we can resolve dependencies during update.
            d->m_reinstallerComponentsDeps.append(component.take());

            const QString& name = d->m_reinstallerComponentsDeps.last()->name();
            const QString replaces = data.package->data(scReplaces).toString();
            installedPackages.take(name);   // remove from local installed packages

            bool isValidUpdate = locals.contains(name);
            if (!isValidUpdate && !replaces.isEmpty()) {
                const QStringList possibleNames = replaces.split(QInstaller::commaRegExp(),
                    QString::SkipEmptyParts);
                foreach(const QString & possibleName, possibleNames) {
                    if (locals.contains(possibleName)) {
                        isValidUpdate = true;
                        replaceMes << possibleName;
                    }
                }
            }

            // break if the reinstaller is not valid and if it's not the maintenance tool (we might get an update
            // for the maintenance tool even if it's not currently installed - possible offline installation)
            if (!isValidUpdate && (update->data(scEssential, scFalse).toString().toLower() == scFalse))
                continue;   // Update for not installed package found, skip it.

            const LocalPackage& localPackage = locals.value(name);

            // package must be equal
            const QDate updateDate = update->data(scReleaseDate).toDate();
            if (localPackage.lastUpdateDate == updateDate)
                continue;

            if (update->data(scEssential, scFalse).toString().toLower() == scTrue ||
                update->data(scForcedUpdate, scFalse).toString().toLower() == scTrue) {
                setFoundEssentialUpdate(true);
            }

            // this is not a dependency, it is a real update
            components.insert(name, d->m_reinstallerComponentsDeps.takeLast());
        }
        else {
            return false;
        }
    }

    QHash<QString, QInstaller::Component*> localReplaceMes;
    foreach(const QString & key, installedPackages.keys()) {
        QInstaller::Component* component = new QInstaller::Component(this);
        component->loadDataFromPackage(installedPackages.value(key));
        d->m_reinstallerComponentsDeps.append(component);
        // Keep a list of local components that should be replaced
        if (replaceMes.contains(component->name()))
            localReplaceMes.insert(component->name(), component);
    }

    // store all components that got a replacement, but do not modify the components list
    storeReplacedComponents(localReplaceMes.unite(components), data);

    try {
        if (!components.isEmpty()) {
            // append all components w/o parent to the direct list
            foreach(QInstaller::Component * component, components) {
                appendReinstallerComponent(component);
            }

            // after everything is set up, load the scripts
            foreach(QInstaller::Component * component, components) {
                if (d->statusCanceledOrFailed())
                    return false;

                component->loadComponentScript();
                if (!component->isUnstable())
                    component->setCheckState(Qt::Checked);
            }

            // after everything is set up, check installed components
            foreach(QInstaller::Component * component, d->m_reinstallerComponentsDeps) {
                if (d->statusCanceledOrFailed())
                    return false;
                // even for possible dependency we need to load the script for example to get archives
                component->loadComponentScript();
                if (component->isInstalled()) {
                    // since we do not put them into the model, which would force a update of e.g. tri state
                    // components, we have to check all installed components ourselves
                    if (!component->isUnstable())
                        component->setCheckState(Qt::Checked);
                }
            }

            if (foundEssentialUpdate()) {
                foreach(QInstaller::Component * component, components) {
                    if (d->statusCanceledOrFailed())
                        return false;

                    component->setCheckable(false);
                    component->setSelectable(false);
                    if ((component->value(scEssential, scFalse).toLower() == scTrue)
                        || (component->value(scForcedUpdate, scFalse).toLower() == scTrue)) {
                        // essential updates are enabled, still not checkable but checked
                        component->setEnabled(true);
                    }
                    else {
                        // non essential updates are disabled, not checkable and unchecked
                        component->setEnabled(false);
                        component->setCheckState(Qt::Unchecked);
                    }
                }
            }

            std::sort(d->m_reinstallerComponents.begin(), d->m_reinstallerComponents.end(),
                Component::SortingPriorityGreaterThan());
        }
        else {
            // we have no updates, no need to store possible dependencies
            d->clearReinstallerComponentLists();
        }
    }
    catch (const Error& error) {
        d->clearReinstallerComponentLists();
        emit finishReinstallerComponentsReset(QList<QInstaller::Component*>());
        d->setStatus(Failure, error.message());

        // TODO: make sure we remove all message boxes inside the library at some point.
        MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(), QLatin1String("Error"),
            tr("Error"), error.message());
        return false;
    }

    emit finishReinstallerComponentsReset(d->m_reinstallerComponents);
    return true;
}

bool PackageManagerCore::fetchUpdaterPackages(const PackagesList &remotes, const LocalPackagesHash &locals)
{
    emit startUpdaterComponentsReset();

    d->clearUpdaterComponentLists();
    QHash<QString, QInstaller::Component *> components;

    Data data;
    data.components = &components;
    data.installedPackages = &locals;

    setFoundEssentialUpdate(false);
    LocalPackagesHash installedPackages = locals;
    QStringList replaceMes;

    foreach (Package *const update, remotes) {
        if (d->statusCanceledOrFailed())
            return false;

        if (!ProductKeyCheck::instance()->isValidPackage(update->data(scName).toString()))
            continue;

        QScopedPointer<QInstaller::Component> component(new QInstaller::Component(this));
        data.package = update;
        component->loadDataFromPackage(*update);
        if (updateComponentData(data, component.data())) {
            // Keep a reference so we can resolve dependencies during update.
            d->m_updaterComponentsDeps.append(component.take());

            const QString &name = d->m_updaterComponentsDeps.last()->name();
            const QString replaces = data.package->data(scReplaces).toString();
            installedPackages.take(name);   // remove from local installed packages

            bool isValidUpdate = locals.contains(name);
            if (!isValidUpdate && !replaces.isEmpty()) {
                const QStringList possibleNames = replaces.split(QInstaller::commaRegExp(),
                    QString::SkipEmptyParts);
                foreach (const QString &possibleName, possibleNames) {
                    if (locals.contains(possibleName)) {
                        isValidUpdate = true;
                        replaceMes << possibleName;
                    }
                }
            }

            // break if the update is not valid and if it's not the maintenance tool (we might get an update
            // for the maintenance tool even if it's not currently installed - possible offline installation)
            if (!isValidUpdate && (update->data(scEssential, scFalse).toString().toLower() == scFalse))
                continue;   // Update for not installed package found, skip it.

            const LocalPackage &localPackage = locals.value(name);
            if (!d->packageNeedsUpdate(localPackage, update))
                continue;
            // It is quite possible that we may have already installed the update. Lets check the last
            // update date of the package and the release date of the update. This way we can compare and
            // figure out if the update has been installed or not.
            const QDate updateDate = update->data(scReleaseDate).toDate();
            if (localPackage.lastUpdateDate > updateDate)
                continue;

            if (update->data(scEssential, scFalse).toString().toLower() == scTrue ||
                    update->data(scForcedUpdate, scFalse).toString().toLower() == scTrue) {
                setFoundEssentialUpdate(true);
            }

            // this is not a dependency, it is a real update
            components.insert(name, d->m_updaterComponentsDeps.takeLast());
        } else {
            return false;
        }
    }

    QHash<QString, QInstaller::Component *> localReplaceMes;
    foreach (const QString &key, installedPackages.keys()) {
        QInstaller::Component *component = new QInstaller::Component(this);
        component->loadDataFromPackage(installedPackages.value(key));
        d->m_updaterComponentsDeps.append(component);
        // Keep a list of local components that should be replaced
        if (replaceMes.contains(component->name()))
            localReplaceMes.insert(component->name(), component);
    }

    // store all components that got a replacement, but do not modify the components list
    storeReplacedComponents(localReplaceMes.unite(components), data);

    try {
        if (!components.isEmpty()) {
            // append all components w/o parent to the direct list
            foreach (QInstaller::Component *component, components) {
                appendUpdaterComponent(component);
            }

            // after everything is set up, load the scripts
            foreach (QInstaller::Component *component, components) {
                if (d->statusCanceledOrFailed())
                    return false;

                component->loadComponentScript();
                if (!component->isUnstable())
                    component->setCheckState(Qt::Checked);
            }

            // after everything is set up, check installed components
            foreach (QInstaller::Component *component, d->m_updaterComponentsDeps) {
                if (d->statusCanceledOrFailed())
                    return false;
                // even for possible dependency we need to load the script for example to get archives
                component->loadComponentScript();
                if (component->isInstalled()) {
                    // since we do not put them into the model, which would force a update of e.g. tri state
                    // components, we have to check all installed components ourselves
                    if (!component->isUnstable())
                        component->setCheckState(Qt::Checked);
                }
            }

            if (foundEssentialUpdate()) {
                foreach (QInstaller::Component *component, components) {
                    if (d->statusCanceledOrFailed())
                        return false;

                    component->setCheckable(false);
                    component->setSelectable(false);
                    if ((component->value(scEssential, scFalse).toLower() == scTrue)
                        || (component->value(scForcedUpdate, scFalse).toLower() == scTrue)) {
                        // essential updates are enabled, still not checkable but checked
                        component->setEnabled(true);
                    } else {
                        // non essential updates are disabled, not checkable and unchecked
                        component->setEnabled(false);
                        component->setCheckState(Qt::Unchecked);
                    }
                }
            }

            std::sort(d->m_updaterComponents.begin(), d->m_updaterComponents.end(),
                Component::SortingPriorityGreaterThan());
        } else {
            // we have no updates, no need to store possible dependencies
            d->clearUpdaterComponentLists();
        }
    } catch (const Error &error) {
        d->clearUpdaterComponentLists();
        emit finishUpdaterComponentsReset(QList<QInstaller::Component*>());
        d->setStatus(Failure, error.message());

        // TODO: make sure we remove all message boxes inside the library at some point.
        MessageBoxHandler::critical(MessageBoxHandler::currentBestSuitParent(), QLatin1String("Error"),
            tr("Error"), error.message());
        return false;
    }

    emit finishUpdaterComponentsReset(d->m_updaterComponents);
    return true;
}

void PackageManagerCore::restoreCheckState()
{
    d->restoreCheckState();
}

void PackageManagerCore::updateDisplayVersions(const QString &displayKey)
{
    QHash<QString, QInstaller::Component *> componentsHash;
    foreach (QInstaller::Component *component, components(ComponentType::All))
        componentsHash[component->name()] = component;

    // set display version for all components in list
    const QStringList &keys = componentsHash.keys();
    foreach (const QString &key, keys) {
        QHash<QString, bool> visited;
        if (componentsHash.value(key)->isInstalled()) {
            componentsHash.value(key)->setValue(scDisplayVersion,
                findDisplayVersion(key, componentsHash, scInstalledVersion, visited));
        }
        visited.clear();
        const QString displayVersionRemote = findDisplayVersion(key, componentsHash,
            scVersion, visited);
        if (displayVersionRemote.isEmpty())
            componentsHash.value(key)->setValue(displayKey, tr("Invalid"));
        else
            componentsHash.value(key)->setValue(displayKey, displayVersionRemote);
    }

}

QString PackageManagerCore::findDisplayVersion(const QString &componentName,
    const QHash<QString, Component *> &components, const QString &versionKey, QHash<QString, bool> &visited)
{
    if (!components.contains(componentName))
        return QString();
    const QString replaceWith = components.value(componentName)->value(scInheritVersion);
    visited[componentName] = true;

    if (replaceWith.isEmpty())
        return components.value(componentName)->value(versionKey);

    if (visited.contains(replaceWith))  // cycle
        return QString();

    return findDisplayVersion(replaceWith, components, versionKey, visited);
}

ComponentModel *PackageManagerCore::componentModel(PackageManagerCore *core, const QString &objectName) const
{
    ComponentModel *model = new ComponentModel(ComponentModelHelper::LastColumn, core);

    model->setObjectName(objectName);
    model->setHeaderData(ComponentModelHelper::NameColumn, Qt::Horizontal,
        ComponentModel::tr("Component Name"));
    model->setHeaderData(ComponentModelHelper::ActionColumn, Qt::Horizontal,
        ComponentModel::tr("Action"));
    model->setHeaderData(ComponentModelHelper::InstalledVersionColumn, Qt::Horizontal,
        ComponentModel::tr("Installed Version"));
    model->setHeaderData(ComponentModelHelper::NewVersionColumn, Qt::Horizontal,
        ComponentModel::tr("New Version"));
    model->setHeaderData(ComponentModelHelper::ReleaseDateColumn, Qt::Horizontal,
        ComponentModel::tr("Release Date"));
    model->setHeaderData(ComponentModelHelper::UncompressedSizeColumn, Qt::Horizontal,
        ComponentModel::tr("Size"));
    connect(model, SIGNAL(checkStateChanged(QInstaller::ComponentModel::ModelState)), this,
        SLOT(componentsToInstallNeedsRecalculation()));

    return model;
}

/*!
    Returns the file list used for delayed deletion.
*/
QStringList PackageManagerCore::filesForDelayedDeletion() const
{
    return d->m_filesForDelayedDeletion;
}

/*!
    Adds \a files for delayed deletion.
*/
void PackageManagerCore::addFilesForDelayedDeletion(const QStringList &files)
{
    d->m_filesForDelayedDeletion.append(files);
}

/*!
    Adds a colon symbol to the component \c name as a separator between
    component \a name and version.
*/
QString PackageManagerCore::checkableName(const QString &name)
{
    // to ensure backward compatibility, fix component name with dash (-) symbol
    if (!name.contains(QLatin1Char(':')))
        if (name.contains(QLatin1Char('-')))
            return name + QLatin1Char(':');

    return name;
}

/*!
    Parses \a name and \a version from \a requirement component. \c requirement
    contains both \a name and \a version separated either with ':' or with '-'.
*/
void PackageManagerCore::parseNameAndVersion(const QString &requirement, QString *name, QString *version)
{
    if (requirement.isEmpty()) {
        if (name)
            name->clear();
        if (version)
            version->clear();
        return;
    }

    int pos = requirement.indexOf(QLatin1Char(':'));
    // to ensure backward compatibility, check dash (-) symbol too
    if (pos == -1)
        pos = requirement.indexOf(QLatin1Char('-'));
    if (pos != -1) {
        if (name)
            *name = requirement.left(pos);
        if (version)
            *version = requirement.mid(pos + 1);
    } else {
        if (name)
            *name = requirement;
        if (version)
            version->clear();
    }
}

/*!
    Excludes version numbers from names from \a requirements components.
    \a requirements list contains names that have both name and version.

    Returns a list containing names without version numbers.
*/
QStringList PackageManagerCore::parseNames(const QStringList &requirements)
{
    QString name;
    QString version;
    QStringList names;
    foreach (const QString &requirement, requirements) {
        parseNameAndVersion(requirement, &name, &version);
        names.append(name);
    }
    return names;
}
