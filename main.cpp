#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QFileDialog>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QMouseEvent>
#include <QDir>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QLinearGradient>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>
#include <QCloseEvent>
#include <QToolTip>
#include <QFileInfo>
#include <QProcess>

class MediaControlWidget : public QWidget {
    Q_OBJECT
public:
    MediaControlWidget(QWidget *parent = nullptr)
    : QWidget(parent), mediaLoaded(false), isPlaying(false), currentMediaPath("") {
        setupUI();
        setupPlayer();
        // Create musiclist.txt if it doesn't exist
        QFile file("musiclist.txt");
        if (!file.exists()) {
            file.open(QIODevice::WriteOnly);
            file.close();
        }
        updateTimer = new QTimer(this);
        connect(updateTimer, &QTimer::timeout, this, &MediaControlWidget::updateProgress);
        updateTimer->start(100);
    }
    ~MediaControlWidget() { resetPlayer(); }
    void showControlPanel() {
        QPoint pos = QCursor::pos();
        move(pos.x() - width() / 2, pos.y() - height());
        show();
        raise();
        activateWindow();
    }
protected:
    void closeEvent(QCloseEvent *event) override {
        resetPlayer();
        event->accept();
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::ToolTip) {
            QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
            QPoint pos = helpEvent->pos();
            QWidget *widget = childAt(pos);
            if (widget && widget->inherits("QPushButton")) {
                QPushButton *button = qobject_cast<QPushButton *>(widget);
                QToolTip::showText(helpEvent->globalPos(), button->toolTip());
                return true;
            }
        }
        return QWidget::event(event);
    }
private slots:
    void openMediaFile() {
        QString fileName = QFileDialog::getOpenFileName(
            this, tr("Open Media File"),
                                                        QStandardPaths::standardLocations(QStandardPaths::MusicLocation).value(0, QDir::homePath()),
                                                        tr("Media Files (*.mp3 *.mp4 *.wav *.ogg *.flac)"));
        if (!fileName.isEmpty()) {
            loadMediaFile(fileName);
        }
    }
    void togglePlayPause() {
        if (!mediaLoaded) {
            openMediaFile();
            return;
        }
        if (isPlaying) {
            player->pause();
            playButton->setIcon(QIcon(":/images/play.png"));
            // Play "pause" sound
            QMediaPlayer *soundPlayer = new QMediaPlayer(this);
            QAudioOutput *audio = new QAudioOutput(this);
            soundPlayer->setAudioOutput(audio);
            soundPlayer->setSource(QUrl("qrc:/sounds/pause.mp3"));
            soundPlayer->play();
        } else {
            player->play();
            playButton->setIcon(QIcon(":/images/pause.png"));
            // Play "play" sound
            QMediaPlayer *soundPlayer = new QMediaPlayer(this);
            QAudioOutput *audio = new QAudioOutput(this);
            soundPlayer->setAudioOutput(audio);
            soundPlayer->setSource(QUrl("qrc:/sounds/play.mp3"));
            soundPlayer->play();
        }
        isPlaying = !isPlaying;
    }
    void skipForward() {
        if (mediaLoaded) {
            player->setPosition(player->position() + 10000);
            // Play "click" sound
            QMediaPlayer *soundPlayer = new QMediaPlayer(this);
            QAudioOutput *audio = new QAudioOutput(this);
            soundPlayer->setAudioOutput(audio);
            soundPlayer->setSource(QUrl("qrc:/sounds/click.mp3"));
            soundPlayer->play();
        }
    }
    void skipForwardDouble() {
        if (mediaLoaded) {
            player->setPosition(player->position() + 30000);
            // Play "click" sound
            QMediaPlayer *soundPlayer = new QMediaPlayer(this);
            QAudioOutput *audio = new QAudioOutput(this);
            soundPlayer->setAudioOutput(audio);
            soundPlayer->setSource(QUrl("qrc:/sounds/click.mp3"));
            soundPlayer->play();
        }
    }
    void saveCurrentSong() {
        if (!mediaLoaded || currentMediaPath.isEmpty()) {
            QMessageBox::information(this, "Info", "No media loaded to save");
            return;
        }
        QFile file("musiclist.txt");
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << currentMediaPath << "\n";
            file.close();
            // Play "save" sound
            QMediaPlayer *soundPlayer = new QMediaPlayer(this);
            QAudioOutput *audio = new QAudioOutput(this);
            soundPlayer->setAudioOutput(audio);
            soundPlayer->setSource(QUrl("qrc:/sounds/save.mp3"));
            soundPlayer->play();
            QMessageBox::information(this, "Saved", "Current song added to playlist");
        } else {
            QMessageBox::warning(this, "Error", "Could not save to playlist file");
        }
    }
    void loadPlaylist() {
        // Play "select" sound
        QMediaPlayer *soundPlayer = new QMediaPlayer(this);
        QAudioOutput *audio = new QAudioOutput(this);
        soundPlayer->setAudioOutput(audio);
        soundPlayer->setSource(QUrl("qrc:/sounds/select.mp3"));
        soundPlayer->play();
        QFile file("musiclist.txt");
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Error", "Could not open playlist file");
            return;
        }
        QStringList paths;
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty() && QFile::exists(line)) {
                paths << line;
            }
        }
        file.close();
        if (paths.isEmpty()) {
            QMessageBox::information(this, "Info", "Playlist is empty or contains invalid paths");
            return;
        }
        bool ok;
        QString item = QInputDialog::getItem(this, "Load Playlist", "Select a song:", paths, 0, false, &ok);
        if (ok && !item.isEmpty()) {
            loadMediaFile(item);
        }
    }
    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            playButton->setIcon(QIcon(":/images/play.png"));
            isPlaying = false;
            player->setPosition(0);
            updateTimeDisplay();
        } else if (status == QMediaPlayer::LoadedMedia) {
            mediaLoaded = true;
            isPlaying = true;
            playButton->setIcon(QIcon(":/images/pause.png"));
            player->play();
            updateTimeDisplay();
            updateFileNameDisplay();
        }
    }
    void handleError(QMediaPlayer::Error error, const QString &errorString) {
        Q_UNUSED(error);
        QMessageBox::warning(this, tr("Error"), errorString);
        resetPlayer();
    }
    void updateProgress() {
        if (mediaLoaded && isPlaying) {
            updateTimeDisplay();
        }
    }
    void updateTimeDisplay() {
        if (!mediaLoaded) return;
        qint64 position = player->position();
        qint64 duration = player->duration();
        QString positionTime = formatTime(position);
        QString durationTime = formatTime(duration);
        timeLabel->setText(QString("%1 / %2").arg(positionTime, durationTime));
        if (duration > 0) {
            progressBar->setMaximum(1000);
            progressBar->setValue(static_cast<int>((position * 1000) / duration));
        }
    }
    void updateFileNameDisplay() {
        if (!mediaLoaded || currentMediaPath.isEmpty()) {
            fileNameLabel->setText("No file loaded");
            return;
        }
        QFileInfo fileInfo(currentMediaPath);
        QString fileName = fileInfo.fileName();
        fileNameLabel->setText(fileName);
        fileNameLabel->setToolTip(currentMediaPath);
    }
    QString formatTime(qint64 milliseconds) {
        int seconds = (milliseconds / 1000) % 60;
        int minutes = (milliseconds / (1000 * 60)) % 60;
        return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (obj == skipButton && event->type() == QEvent::MouseButtonDblClick) {
            skipForwardDouble();
            return true;
        }
        return QWidget::eventFilter(obj, event);
    }
private:
    void loadMediaFile(const QString &fileName) {
        resetPlayer();
        player->setSource(QUrl::fromLocalFile(fileName));
        currentMediaPath = fileName;
        updateFileNameDisplay();
    }
    void resetPlayer() {
        if (player) {
            player->stop();
            player->setSource(QUrl());
        }
        mediaLoaded = false;
        isPlaying = false;
        currentMediaPath = "";
        playButton->setIcon(QIcon(":/images/play.png"));
        timeLabel->setText("0:00 / 0:00");
        fileNameLabel->setText("No file loaded");
        progressBar->setValue(0);
    }
    void setupUI() {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet("QWidget { background-color: #222; border-radius: 8px; padding: 5px; }"
        "QPushButton { background: transparent; border: none; padding: 5px; }"
        "QPushButton:hover { background: #444; border-radius: 4px; }"
        "QToolTip { color: #ffffff; background-color: #333; border: 1px solid #555; }");
        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(5);

        // Add ApexMusic v1.01 label
        QLabel *apexMusicLabel = new QLabel("ApexMusic v1.01", this);
        apexMusicLabel->setAlignment(Qt::AlignCenter);
        apexMusicLabel->setStyleSheet("QLabel { color: #00ffff; font-size: 12px; font-weight: bold; }");
        mainLayout->addWidget(apexMusicLabel);

        // File name label
        fileNameLabel = new QLabel("No file loaded", this);
        fileNameLabel->setAlignment(Qt::AlignCenter);
        fileNameLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 10px; }");
        fileNameLabel->setMaximumWidth(200);
        fileNameLabel->setWordWrap(true);
        mainLayout->addWidget(fileNameLabel);

        progressBar = new QProgressBar(this);
        progressBar->setTextVisible(false);
        progressBar->setStyleSheet(
            "QProgressBar {"
            "  border: 1px solid #444;"
            "  border-radius: 4px;"
            "  background: #333;"
            "  height: 6px;"
            "}"
            "QProgressBar::chunk {"
            "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
            "    stop:0 #00ffff, stop:1 #00aaff);"
            "  border-radius: 4px;"
            "}");
        progressBar->setToolTip("Playback progress");
        mainLayout->addWidget(progressBar);

        timeLabel = new QLabel("0:00 / 0:00", this);
        timeLabel->setAlignment(Qt::AlignCenter);
        timeLabel->setStyleSheet("QLabel { color: #00ffff; font-size: 10px; }");
        timeLabel->setToolTip("Current time / Total time");
        mainLayout->addWidget(timeLabel);

        QHBoxLayout *buttonLayout = new QHBoxLayout();
        buttonLayout->setSpacing(5);

        QPushButton *backButton = new QPushButton(this);
        backButton->setIcon(QIcon(":/images/back.png"));
        backButton->setIconSize(QSize(24, 24));
        backButton->setToolTip("Back 5 seconds");
        connect(backButton, &QPushButton::clicked, this, [this]() {
            if (mediaLoaded) {
                player->setPosition(player->position() - 5000);
                // Play "click" sound
                QMediaPlayer *soundPlayer = new QMediaPlayer(this);
                QAudioOutput *audio = new QAudioOutput(this);
                soundPlayer->setAudioOutput(audio);
                soundPlayer->setSource(QUrl("qrc:/sounds/click.mp3"));
                soundPlayer->play();
            }
        });
        buttonLayout->addWidget(backButton);

        playButton = new QPushButton(this);
        playButton->setIcon(QIcon(":/images/play.png"));
        playButton->setIconSize(QSize(24, 24));
        playButton->setToolTip("Play/Pause");
        connect(playButton, &QPushButton::clicked, this, &MediaControlWidget::togglePlayPause);
        buttonLayout->addWidget(playButton);

        skipButton = new QPushButton(this);
        skipButton->setIcon(QIcon(":/images/skip.png"));
        skipButton->setIconSize(QSize(24, 24));
        skipButton->setToolTip("Skip 10 seconds\nDouble-click for 30 seconds");
        connect(skipButton, &QPushButton::clicked, this, &MediaControlWidget::skipForward);
        skipButton->installEventFilter(this);
        buttonLayout->addWidget(skipButton);

        QPushButton *saveCurrentButton = new QPushButton(this);
        saveCurrentButton->setIcon(QIcon(":/images/save.png"));
        saveCurrentButton->setIconSize(QSize(24, 24));
        saveCurrentButton->setToolTip("Save current song to playlist");
        connect(saveCurrentButton, &QPushButton::clicked, this, &MediaControlWidget::saveCurrentSong);
        buttonLayout->addWidget(saveCurrentButton);

        QPushButton *loadPlaylistButton = new QPushButton(this);
        loadPlaylistButton->setIcon(QIcon(":/images/savelist.png"));
        loadPlaylistButton->setIconSize(QSize(24, 24));
        loadPlaylistButton->setToolTip("Load from playlist");
        connect(loadPlaylistButton, &QPushButton::clicked, this, &MediaControlWidget::loadPlaylist);
        buttonLayout->addWidget(loadPlaylistButton);

        mainLayout->addLayout(buttonLayout);
        setLayout(mainLayout);
        adjustSize();
    }
    void setupPlayer() {
        player = new QMediaPlayer(this);
        audioOutput = new QAudioOutput(this);
        player->setAudioOutput(audioOutput);
        connect(player, &QMediaPlayer::mediaStatusChanged, this, &MediaControlWidget::handleMediaStatusChanged);
        connect(player, &QMediaPlayer::errorOccurred, this, &MediaControlWidget::handleError);
        connect(player, &QMediaPlayer::positionChanged, this, &MediaControlWidget::updateTimeDisplay);
    }
    QMediaPlayer *player;
    QAudioOutput *audioOutput;
    QPushButton *playButton;
    QPushButton *skipButton;
    QProgressBar *progressBar;
    QLabel *timeLabel;
    QLabel *fileNameLabel;
    QTimer *updateTimer;
    bool mediaLoaded;
    bool isPlaying;
    QString currentMediaPath;
};

class TrayIcon : public QSystemTrayIcon {
    Q_OBJECT
public:
    TrayIcon(QObject *parent = nullptr)
    : QSystemTrayIcon(parent) {
        setIcon(QIcon(":/images/icon.png"));
        mediaWidget = new MediaControlWidget();
        QMenu *menu = new QMenu();
        QAction *quitAction = menu->addAction("Quit");
        connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
        setContextMenu(menu);
        connect(this, &QSystemTrayIcon::activated, this, &TrayIcon::onTrayIconActivated);
    }
private slots:
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            mediaWidget->showControlPanel();
        }
    }
private:
    MediaControlWidget *mediaWidget;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Media Control Widget");
    app.setOrganizationName("Plasma Widget");
    app.setQuitOnLastWindowClosed(false);
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "Error", "System tray not available");
        return 1;
    }
    TrayIcon trayIcon;
    trayIcon.show();
    return app.exec();
}
#include "main.moc"
