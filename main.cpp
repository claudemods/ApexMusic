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

class MediaControlWidget : public QWidget
{
    Q_OBJECT

public:
    MediaControlWidget(QWidget *parent = nullptr)
    : QWidget(parent), mediaLoaded(false), isPlaying(false)
    {
        setupUI();
        setupPlayer();

        // Timer for updating progress bar
        updateTimer = new QTimer(this);
        connect(updateTimer, &QTimer::timeout, this, &MediaControlWidget::updateProgress);
        updateTimer->start(100); // Update every 100ms for smooth animation
    }

    void showControlPanel()
    {
        QPoint pos = QCursor::pos();
        move(pos.x() - width()/2, pos.y() - height());
        show();
        raise();
        activateWindow();
    }

private slots:
    void openMediaFile()
    {
        QString fileName = QFileDialog::getOpenFileName(this,
                                                        tr("Open Media File"),
                                                        QStandardPaths::standardLocations(QStandardPaths::MusicLocation).value(0, QDir::homePath()),
                                                        tr("Media Files (*.mp3 *.mp4 *.wav *.ogg *.flac)"));

        if (!fileName.isEmpty()) {
            loadMediaFile(fileName);
        }
    }

    void togglePlayPause()
    {
        if (!mediaLoaded) {
            openMediaFile();
            return;
        }

        if (isPlaying) {
            player->pause();
            playButton->setIcon(QIcon(":/images/play.png"));
        } else {
            player->play();
            playButton->setIcon(QIcon(":/images/pause.png"));
        }
        isPlaying = !isPlaying;
    }

    void skipForward()
    {
        if (mediaLoaded) {
            player->setPosition(player->position() + 10000);
        }
    }

    void skipForwardDouble()
    {
        if (mediaLoaded) {
            player->setPosition(player->position() + 30000);
        }
    }

    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
    {
        if (status == QMediaPlayer::EndOfMedia) {
            playButton->setIcon(QIcon(":/images/play.png"));
            isPlaying = false;
            player->setPosition(0); // Reset to beginning
            updateTimeDisplay(); // Update time display to show 0:00
        } else if (status == QMediaPlayer::LoadedMedia) {
            mediaLoaded = true;
            isPlaying = true;
            playButton->setIcon(QIcon(":/images/pause.png"));
            player->play();
            updateTimeDisplay(); // Update time display immediately
        }
    }

    void handleError(QMediaPlayer::Error error, const QString &errorString)
    {
        Q_UNUSED(error);
        QMessageBox::warning(this, tr("Error"), errorString);
        mediaLoaded = false;
        isPlaying = false;
        playButton->setIcon(QIcon(":/images/play.png"));
    }

    void updateProgress()
    {
        if (mediaLoaded && isPlaying) {
            updateTimeDisplay();
        }
    }

    void updateTimeDisplay()
    {
        if (!mediaLoaded) return;

        qint64 position = player->position();
        qint64 duration = player->duration();

        // Format time as minutes:seconds
        QString positionTime = formatTime(position);
        QString durationTime = formatTime(duration);
        timeLabel->setText(QString("%1 / %2").arg(positionTime, durationTime));

        // Update progress bar
        if (duration > 0) {
            progressBar->setMaximum(1000); // Use 1000 steps for smoother animation
            progressBar->setValue(static_cast<int>((position * 1000) / duration));
        }
    }

    QString formatTime(qint64 milliseconds)
    {
        int seconds = (milliseconds / 1000) % 60;
        int minutes = (milliseconds / (1000 * 60)) % 60;
        return QString("%1:%2").arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (obj == skipButton && event->type() == QEvent::MouseButtonDblClick) {
            skipForwardDouble();
            return true;
        }
        return QWidget::eventFilter(obj, event);
    }

private:
    void loadMediaFile(const QString &fileName)
    {
        player->setSource(QUrl::fromLocalFile(fileName));
    }

    void setupUI()
    {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet("QWidget { background-color: #222; border-radius: 8px; padding: 5px; }"
        "QPushButton { background: transparent; border: none; padding: 5px; }"
        "QPushButton:hover { background: #444; border-radius: 4px; }");

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(5);

        // Create progress bar with custom style
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
        mainLayout->addWidget(progressBar);

        // Time display label
        timeLabel = new QLabel("0:00 / 0:00", this);
        timeLabel->setAlignment(Qt::AlignCenter);
        timeLabel->setStyleSheet("QLabel { color: #00ffff; font-size: 10px; }");
        mainLayout->addWidget(timeLabel);

        QHBoxLayout *buttonLayout = new QHBoxLayout();
        buttonLayout->setSpacing(5);

        // Back button
        QPushButton *backButton = new QPushButton(this);
        backButton->setIcon(QIcon(":/images/back.png"));
        backButton->setIconSize(QSize(24, 24));
        connect(backButton, &QPushButton::clicked, this, [this]() {
            if (mediaLoaded) player->setPosition(player->position() - 5000);
        });
            buttonLayout->addWidget(backButton);

            // Play/Pause button
            playButton = new QPushButton(this);
            playButton->setIcon(QIcon(":/images/play.png"));
            playButton->setIconSize(QSize(24, 24));
            connect(playButton, &QPushButton::clicked, this, &MediaControlWidget::togglePlayPause);
            buttonLayout->addWidget(playButton);

            // Skip button
            skipButton = new QPushButton(this);
            skipButton->setIcon(QIcon(":/images/skip.png"));
            skipButton->setIconSize(QSize(24, 24));
            connect(skipButton, &QPushButton::clicked, this, &MediaControlWidget::skipForward);
            skipButton->installEventFilter(this);
            buttonLayout->addWidget(skipButton);

            mainLayout->addLayout(buttonLayout);
            setLayout(mainLayout);
            adjustSize();
    }

    void setupPlayer()
    {
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
    QTimer *updateTimer;
    bool mediaLoaded;
    bool isPlaying;
};

class TrayIcon : public QSystemTrayIcon
{
    Q_OBJECT

public:
    TrayIcon(QObject *parent = nullptr)
    : QSystemTrayIcon(parent)
    {
        setIcon(QIcon(":/images/icon.png"));
        mediaWidget = new MediaControlWidget();

        // Create context menu
        QMenu *menu = new QMenu();
        QAction *quitAction = menu->addAction("Quit");
        connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
        setContextMenu(menu);

        connect(this, &QSystemTrayIcon::activated, this, &TrayIcon::onTrayIconActivated);
    }

private slots:
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::Trigger) {
            mediaWidget->showControlPanel();
        }
    }

private:
    MediaControlWidget *mediaWidget;
};

int main(int argc, char *argv[])
{
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
