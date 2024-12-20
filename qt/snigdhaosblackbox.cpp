#include "snigdhaosblackbox.h"  // Includes the header file for the SnigdhaOSBlackbox class to use its declarations and functionality.
#include "./ui_snigdhaosblackbox.h"  // Includes the auto-generated header file for the UI created using Qt Designer.

#include <QCheckBox>  // Used to manage checkbox UI components.
#include <QDebug>  // Provides tools for debugging, logging information, and printing messages to the console.
#include <QFileInfo>  // Allows access to file metadata, such as checking file modification times.
#include <QProcess>  // Used to manage and interact with external processes (such as running commands in the terminal).
#include <QScrollArea>  // Provides a scrollable area in the UI to allow navigation through large widgets.
#include <QTemporaryFile>  // Creates temporary files that are automatically deleted after use.
#include <QTimer>  // Provides functionality for scheduling tasks with delays or intervals.
#include <QtNetwork/QNetworkReply>  // Handles responses from network requests (used to check internet connectivity).
#include <unistd.h>  // Provides POSIX functions, used here for process management (e.g., restarting the application).

const char* INTERNET_CHECK_URL = "https://snigdha-os.github.io/";  // URL used to verify internet connectivity by sending a network request.

SnigdhaOSBlackbox::SnigdhaOSBlackbox(QWidget *parent, QString state)
    : QMainWindow(parent)
    , ui(new Ui::SnigdhaOSBlackbox)
{
    this->setWindowIcon(QIcon("/usr/share/pixmaps/snigdhaos-blackbox.svg"));
    ui->setupUi(this);
    this->setWindowFlags(this->windowFlags() & -Qt::WindowCloseButtonHint);
    executable_modify_date = QFileInfo(QCoreApplication::applicationFilePath()).lastModified();
    updateState(state);
}

SnigdhaOSBlackbox::~SnigdhaOSBlackbox()
{
    delete ui;
}

void SnigdhaOSBlackbox::doInternetUpRequest() {
    QNetworkAccessManager* network_manager = new QNetworkAccessManager(this);
    QNetworkReply* network_reply = network_manager->head(QNetworkRequest(QString(INTERNET_CHECK_URL)));

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->start(5000); //5 sec

    connect(timer, &QTimer::timeout, this, [this, timer, network_reply, network_manager]() {
        timer->deleteLater();
        network_reply->abort();
        network_reply->deleteLater();
        network_manager->deleteLater();
        doInternetUpRequest();
    });

    connect(network_reply, &QNetworkReply::finished, this, [this, timer, network_reply, network_manager]() {
        timer->stop();
        timer->deleteLater();
        network_reply->deleteLater();
        network_manager->deleteLater();

        if (network_reply->error() == network_reply->NoError){
            updateState(State::UPDATE);
        }
        else {
            doInternetUpRequest();
        }
    });
}

void SnigdhaOSBlackbox::doUpdate() {
    if (qEnvironmentVariableIsSet("SNIGDHAOS_BLACKBOX_SELFUPDATE")) {
        updateState(State::SELECT);
        return;
    }

    auto process = new QProcess(this);
    QTemporaryFile* file = new QTemporaryFile(this);
    file->open();
    file->setAutoRemove(true);
    process->start("/usr/lib/snigdhaos/launch-terminal", QStringList() << QString("sudo pacman -Syyu 2>&1 && rm \"" + file->fileName() + "\"; read -p 'Press Enter↵ to Exit"));
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process, file](int exitcode, QProcess::ExitStatus status) {
        process->deleteLater();
        file->deleteLater();
        if (exitcode == 0 && !file->exists()) {
            relaunchSelf("POST_UPDATE");
        }
        else {
            relaunchSelf("UPDATE_RETRY");
        }
    });
}

void SnigdhaOSBlackbox::doApply() {
    QStringList packages;
    QStringList setup_commands;
    QStringList prepare_commands;

    auto checkBoxList = ui->selectWidget_tabs->findChildren<QCheckBox*>();
    for (auto checkbox : checkBoxList) {
        if (checkbox->isChecked()) {
            packages += checkbox->property("packages").toStringList();
            setup_commands += checkbox->property("setup_commands").toStringList();
            prepare_commands += checkbox->property("prepare_commands").toStringList();
        }
    }

    if (packages.isEmpty()) {
        updateState(State::SUCCESS);
        return;
    }

    if (packages.contains("podman")) {
        setup_commands += "systemctl enable --now podman.socket";
    }
    if (packages.contains("docker")) {
        setup_commands += "systemctl enable --now docker.socket";
    }
    packages.removeDuplicates();

    QTemporaryFile* prepareFile = new QTemporaryFile(this);
    prepareFile->setAutoRemove(true);
    prepareFile->open();

    QTextStream prepareStream(prepareFile);
    prepareStream << prepare_commands.join('\n');
    prepareFile->close();

    QTemporaryFile* packagesFile = new QTemporaryFile(this);
    packagesFile->setAutoRemove(true);
    packagesFile->open();

    QTextStream packagesStream(packagesFile);
    packagesStream << packages.join(' ');
    packagesFile->close();

    QTemporaryFile* setupFile = new QTemporaryFile(this);
    setupFile->setAutoRemove(true);
    setupFile->open();

    QTextStream setupStream(setupFile);
    setupStream << setup_commands.join('\n');
    setupFile->close();

    auto process = new QProcess(this);
    process->start("/usr/lib/snigdhaos/launch-terminal", QStringList() << QString("/usr/lib/snigdhaos-blackbox/apply.sh \"") + prepareFile->fileName() + "\" \"" + packagesFile->fileName() + "\" \"" + setupFile->fileName() + "\"");
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process, prepareFile, packagesFile, setupFile](int exitcode, QProcess::ExitStatus status) {
        process->deleteLater();
        prepareFile->deleteLater();
        packagesFile->deleteLater();
        setupFile->deleteLater();

        if (exitcode == 0 && !packagesFile->exists()) {
            updateState(State::SELECT);
        }
        else {
            updateState(State::APPLY_RETRY);
        }
    });
}

void SnigdhaOSBlackbox::populateSelectWidget() {
    if (ui->selectWidget_tabs->count() > 1) {
        return;
    }

    auto desktop = qEnvironmentVariable("XDG_DESKTOP_SESSION");
    ui->checkBox_GNOME->setVisible(desktop == "gnome");

    bool isDesktop = false;
    auto chassis = QFile("/sys/class/dmi/id/chassis_type");
    if (chassis.open(QFile::ReadOnly)) {
        QStringList list = { "3", "4", "6", "7", "23", "24" };
        QTextStream in(&chassis);
        isDesktop = list.contains(in.readLine());
    }
    ui->checkBox_Performance->setVisible(isDesktop);

    populateSelectWidget("/usr/lib/snigdhaos-blackbox/webapp.txt", "WEBAPP");
}

void SnigdhaOSBlackbox::populateSelectWidget(QString filename, QString label) {
    QFile file(filename);

    if (file.open(QIODevice::ReadOnly)) {
        QScrollArea* scroll = new QScrollArea(ui->selectWidget_tabs);
        QWidget* tab = new QWidget(scroll);
        QVBoxLayout* layout = new QVBoxLayout(tab);
        QTextStream in(&file);

        while (!in.atEnd()) {
            QString def = in.readLine();
            QString packages = in.readLine();
            QString display = in.readLine();

            auto checkbox = new QCheckBox(tab);
            checkbox->setChecked(def == "true");
            checkbox->setText(display);
            checkbox->setProperty("packages", packages.split(" "));
            layout->addWidget(checkbox);
        }

        scroll->setWidget(tab);
        ui->selectWidget_tabs->addTab(scroll, label);
        file.close();
    }
}

void SnigdhaOSBlackbox::updateState(State state) {
    if (currentState != state) {
        currentState = state;
        this->show();
        this->activateWindow();
        this->raise();

        switch (state) {
        case State::WELCOME:
            ui->mainStackedWidget->setCurrentWidget(ui->textWidget);
            ui->textStackedWidget->setCurrentWidget(ui->textWidget_welcome);
            ui->textWidget_buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            break;

        case State::INTERNET:
            ui->mainStackedWidget->setCurrentWidget(ui->mainStackedWidget);
            ui->waitingWidget_text->setText("Waiting For Internet Connection...");
            doInternetUpRequest();
            break;

        case State::UPDATE:
            ui->mainStackedWidget->setCurrentWidget(ui->waitingWidget);
            ui->waitingWidget_text->setText("Please Wait! Till We Finish The Update...");
            doUpdate();
            break;

        case State::UPDATE_RETRY:
            ui->mainStackedWidget->setCurrentWidget(ui->textWidget);
            ui->textStackedWidget->setCurrentWidget(ui->textWidget_updateRetry);
            ui->textWidget_buttonBox->setStandardButtons(QDialogButtonBox::Yes | QDialogButtonBox::No);
            break;

        case State::QUIT:
            ui->mainStackedWidget->setCurrentWidget(ui->textWidget);
            ui->textStackedWidget->setCurrentWidget(ui->textWidget_quit);
            ui->textWidget_buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Reset);
            break;

        case State::SELECT:
            ui->mainStackedWidget->setCurrentWidget(ui->waitingWidget);
            populateSelectWidget();
            break;

        case State::APPLY:
            ui->mainStackedWidget->setCurrentWidget(ui->waitingWidget);
            ui->waitingWidget_text->setText("We are applying the changes...");
            doApply();
            break;

        case State::APPLY_RETRY:
            ui->mainStackedWidget->setCurrentWidget(ui->textWidget);
            ui->textStackedWidget->setCurrentWidget(ui->textWidget_applyRetry);
            ui->textWidget_buttonBox->setStandardButtons(QDialogButtonBox::Yes | QDialogButtonBox::No | QDialogButtonBox::Reset);
            break;

        case State::SUCCESS:
            ui->mainStackedWidget->setCurrentWidget(ui->textWidget);
            ui->textStackedWidget->setCurrentWidget(ui->textWidget_success);
            ui->textWidget_buttonBox->setStandardButtons(QDialogButtonBox::Ok);
            break;
        }
    }
}

void SnigdhaOSBlackbox::updateState(QString state) {
    if (state == "POST_UPDATE"){
        updateState(State::SELECT);
    }
    else if (state == "UPDATE_RETRY") {
        updateState(State::UPDATE_RETRY);
    }
    else {
        updateState(State::WELCOME);
    }
}

void SnigdhaOSBlackbox::relaunchSelf(QString param) {
    auto binary = QFileInfo(QCoreApplication::applicationFilePath());
    if (executable_modify_date != binary.lastModified()) {
        execlp(binary.absoluteFilePath().toUtf8().constData(), binary.fileName().toUtf8().constData(), param.toUtf8().constData(), NULL);
        exit(0);
    }
    else {
        updateState(param);
    }
}

void SnigdhaOSBlackbox::on_textWidget_buttonBox_clicked(QAbstractButton* button) {
    switch(currentState) {
    case State::WELCOME:
        if (ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::Ok) {
            updateState(State::INTERNET);
        }
        break;

    case State::UPDATE_RETRY:
        if (ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::Yes) {
            updateState(State::INTERNET);
        }
        break;

    case State::APPLY_RETRY:
        if (ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::Yes) {
            updateState(State::APPLY);
        }
        else if (ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::Reset) {
            updateState(State::SELECT);
        }
        break;

    case State::SUCCESS:
        if (ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::Ok) {
            QApplication::quit();
        }
        break;

    case State::QUIT:
        if (ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::No || ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::Ok) {
            QApplication::quit();
        }
        else {
            updateState(State::WELCOME);
        }
        break;
    default:;

    }
    if (ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::No || ui->textWidget_buttonBox->standardButton(button) == QDialogButtonBox::Cancel) {
        updateState(State::QUIT);
    }
}

void SnigdhaOSBlackbox::on_selectWidget_buttonBox_Clicked(QAbstractButton* button) {
    if (ui->selectWidget_buttonBox->standardButton(button) == QDialogButtonBox::Ok) {
        updateState(State::APPLY);
    }
    else {
        updateState(State::QUIT);
    }
}
