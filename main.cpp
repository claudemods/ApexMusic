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
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>
#include <QCloseEvent>
#include <QToolTip>
#include <QFileInfo>
#include <QLinearGradient>
#include <QRandomGenerator>
#include <QGuiApplication>
#include <QScreen>
#include <cmath>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>  // Added missing include

class MediaControlWidget : public QWidget {
    Q_OBJECT
public:
    MediaControlWidget(QWidget *parent = nullptr)
    : QWidget(parent), mediaLoaded(false), isPlaying(false), currentMediaPath(""),
    hoverOverProgress(false), draggingProgress(false), wasPlayingBeforeDrag(false),
    beatPhase(0), lastBeatTime(0), beatIntensity(0) {
        setupUI();
        setupPlayer();
        // Initialize audio levels for visualization
        for (int i = 0; i < 60; ++i) {
            audioLevels.append(0.1f);
            peakLevels.append(0.1f);
            beatLevels.append(0.0f);
        }
        // Create musiclist.txt if it doesn't exist
        QFile file("musiclist.txt");
        if (!file.exists()) {
            file.open(QIODevice::WriteOnly);
            file.close();
        }
        updateTimer = new QTimer(this);
        connect(updateTimer, &QTimer::timeout, this, &MediaControlWidget::updateProgress);
        updateTimer->start(50);
        visualizerTimer = new QTimer(this);
        connect(visualizerTimer, &QTimer::timeout, this, &MediaControlWidget::updateVisualizer);
        visualizerTimer->start(30);
        beatTimer = new QTimer(this);
        connect(beatTimer, &QTimer::timeout, this, &MediaControlWidget::updateBeat);
        beatTimer->start(20);
        setMouseTracking(true);
    }
    ~MediaControlWidget() { resetPlayer(); }

    void showControlPanel() {
        QPoint cursorPos = QCursor::pos();
        QRect screenGeometry = QGuiApplication::primaryScreen()->availableGeometry();
        int x = cursorPos.x() - width() / 2;
        int y = cursorPos.y() - height();
        x = qMax(screenGeometry.left(), qMin(x, screenGeometry.right() - width()));
        y = qMax(screenGeometry.top(), qMin(y, screenGeometry.bottom() - height()));
        move(x, y);
        show();
        raise();
        activateWindow();
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        resetPlayer();
        event->accept();
    }

    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw semi-transparent background
        painter.fillRect(rect(), QColor(0, 86, 143, 180)); // #00568f with 70% opacity

        // Draw audio visualizer in its designated slot
        drawVisualizer(painter);

        // Draw progress bar
        int progressBarHeight = 2;
        progressBarRect = QRect(10, height() - progressBarHeight - 10, width() - 20, progressBarHeight);
        painter.fillRect(progressBarRect, QColor(60, 60, 60, 200));

        if (mediaLoaded && player->duration() > 0) {
            double progress = static_cast<double>(player->position()) / player->duration();
            progress = qBound(0.0, progress, 1.0);
            int progressWidth = static_cast<int>(progress * progressBarRect.width());
            QRect progressRect(progressBarRect.x(), progressBarRect.y(), progressWidth, progressBarRect.height());
            painter.fillRect(progressRect, QColor(36, 255, 255));

            // Draw draggable circle handle
            int handleSize = 12;
            int handleY = progressBarRect.y() - (handleSize - progressBarHeight) / 2;
            int handleX = progressBarRect.x() + progressWidth - handleSize/2;

            if (draggingProgress || hoverOverProgress) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(36, 255, 255));
                painter.drawEllipse(handleX, handleY, handleSize, handleSize);

                // Draw time tooltip near the handle
                QString timeText = formatTime(player->position()) + " / " + formatTime(player->duration());
                QRect tooltipRect(handleX - 30, handleY - 25, 60, 20);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(60, 60, 60, 220));
                painter.drawRoundedRect(tooltipRect, 3, 3);
                painter.setPen(QColor(255, 255, 255));
                painter.drawText(tooltipRect, Qt::AlignCenter, timeText);
            }
        }
    }

    void drawVisualizer(QPainter &painter) {
        if (!mediaLoaded) return;

        // Compact professional visualizer dimensions
        int visualizerHeight = 24;
        int visualizerWidth = width() - 40;
        int visualizerX = 20;
        int visualizerY = 75;

        // Draw visualizer background (less transparent)
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, 220));
        painter.drawRoundedRect(visualizerX, visualizerY, visualizerWidth, visualizerHeight, 2, 2);

        // Draw audio bars with two distinct colors
        int barCount = 16;
        int barWidth = (visualizerWidth - (barCount - 1)) / barCount;
        int spacing = 1;

        for (int i = 0; i < barCount; ++i) {
            int levelIndex = (i * 3) % audioLevels.size();
            float level = isPlaying ? audioLevels[levelIndex] : 0.1f;
            float peak = isPlaying ? peakLevels[levelIndex] : 0.1f;
            float beat = isPlaying ? beatLevels[levelIndex] : 0.0f;

            int barHeight = qMin(static_cast<int>((level + beat * 0.2) * visualizerHeight), visualizerHeight);
            int peakHeight = qMin(static_cast<int>(peak * visualizerHeight), visualizerHeight);

            int x = visualizerX + i * (barWidth + spacing);
            int y = visualizerY + visualizerHeight - barHeight;

            // Alternate between two distinct colors for each bar
            QColor barColor;
            if (i % 2 == 0) {
                barColor = QColor(0, 86, 143); // #00568f
            } else {
                barColor = QColor(36, 255, 255); // #24ffff
            }

            // Apply beat effect
            if (beat > 0.1f) {
                barColor = barColor.lighter(100 + static_cast<int>(beat * 30));
            }

            painter.setBrush(barColor);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(x, y, barWidth, barHeight, 1, 1);

            // Draw peak indicator
            if (peakHeight > barHeight) {
                painter.setBrush(barColor.lighter(130));
                painter.drawRect(x, y - (peakHeight - barHeight), barWidth, 1);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        // Check if mouse is over progress bar area
        QRect hoverRect(10, height() - 25, width() - 20, 20);
        bool wasHovering = hoverOverProgress;
        hoverOverProgress = hoverRect.contains(event->pos());
        if (hoverOverProgress != wasHovering) {
            setCursor(hoverOverProgress ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
        if (draggingProgress && mediaLoaded) {
            // Calculate new position based on mouse X
            int mouseX = event->pos().x() - progressBarRect.x();
            mouseX = qBound(0, mouseX, progressBarRect.width());
            double percentage = static_cast<double>(mouseX) / progressBarRect.width();
            qint64 newPosition = static_cast<qint64>(percentage * player->duration());
            player->setPosition(newPosition);
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && hoverOverProgress && mediaLoaded) {
            draggingProgress = true;
            wasPlayingBeforeDrag = isPlaying;
            // Pause playback during dragging
            if (isPlaying) {
                player->pause();
                isPlaying = false;
                playButton->setIcon(QIcon(":/images/play.png"));
            }
            // Calculate clicked position in media
            int mouseX = event->pos().x() - progressBarRect.x();
            mouseX = qBound(0, mouseX, progressBarRect.width());
            double percentage = static_cast<double>(mouseX) / progressBarRect.width();
            qint64 newPosition = static_cast<qint64>(percentage * player->duration());
            player->setPosition(newPosition);
            update();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && draggingProgress) {
            draggingProgress = false;
            // Resume playback if it was playing before drag
            if (wasPlayingBeforeDrag) {
                player->play();
                isPlaying = true;
                playButton->setIcon(QIcon(":/images/pause.png"));
            }
            update();
        }
        QWidget::mouseReleaseEvent(event);
    }

    bool event(QEvent *event) override {
        if (event->type() == QEvent::ToolTip) {
            QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
            QPoint pos = helpEvent->pos();
            QWidget *widget = childAt(pos);
            if (widget && widget->inherits("QPushButton")) {
                QPushButton *button = qobject_cast<QPushButton *>(widget);
                QToolTip::showText(helpEvent->globalPos(), button->toolTip(), this, QRect(), 3000);
                return true;
            }
        }
        return QWidget::event(event);
    }

private slots:
    void animateButton(QPushButton* button) {
        QPropertyAnimation *animation = new QPropertyAnimation(button, "geometry");
        animation->setDuration(100);
        animation->setStartValue(button->geometry());
        animation->setEndValue(button->geometry().adjusted(-2, -2, 2, 2));

        QPropertyAnimation *reverse = new QPropertyAnimation(button, "geometry");
        reverse->setDuration(100);
        reverse->setStartValue(button->geometry().adjusted(-2, -2, 2, 2));
        reverse->setEndValue(button->geometry());

        QSequentialAnimationGroup *group = new QSequentialAnimationGroup(this);
        group->addAnimation(animation);
        group->addAnimation(reverse);
        group->start(QAbstractAnimation::DeleteWhenStopped);
    }

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
        animateButton(qobject_cast<QPushButton*>(sender()));
        if (isPlaying) {
            player->pause();
            playButton->setIcon(QIcon(":/images/play.png"));
        } else {
            player->play();
            playButton->setIcon(QIcon(":/images/pause.png"));
        }
        isPlaying = !isPlaying;
        update();
    }

    void skipForward() {
        animateButton(qobject_cast<QPushButton*>(sender()));
        if (mediaLoaded) {
            player->setPosition(player->position() + 10000);
            update();
        }
    }

    void saveCurrentSong() {
        animateButton(qobject_cast<QPushButton*>(sender()));
        if (!mediaLoaded || currentMediaPath.isEmpty()) {
            QMessageBox::information(this, "Info", "No media loaded to save");
            return;
        }
        QFile file("musiclist.txt");
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << currentMediaPath << "\n";
            file.close();
            QMessageBox::information(this, "Saved", "Current song added to playlist");
        } else {
            QMessageBox::warning(this, "Error", "Could not save to playlist file");
        }
    }

    void loadPlaylist() {
        animateButton(qobject_cast<QPushButton*>(sender()));
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
            update();
        } else if (status == QMediaPlayer::LoadedMedia) {
            mediaLoaded = true;
            isPlaying = true;
            playButton->setIcon(QIcon(":/images/pause.png"));
            player->play();
            updateTimeDisplay();
            updateFileNameDisplay();
            update();
        }
    }

    void handleError(QMediaPlayer::Error error, const QString &errorString) {
        Q_UNUSED(error);
        QMessageBox::warning(this, tr("Error"), errorString);
        resetPlayer();
    }

    void updateProgress() {
        if (mediaLoaded && isPlaying && !draggingProgress) {
            updateTimeDisplay();
            update();
        }
    }

    void updateVisualizer() {
        if (!mediaLoaded) return;

        for (int i = 0; i < audioLevels.size(); ++i) {
            float baseLevel = isPlaying ? 0.3f : 0.1f;
            float wave = qSin((i + visualizerPhase) * 0.2f) * 0.2f;
            float random = (QRandomGenerator::global()->generate() % 30) / 100.0f;

            float beatEffect = beatLevels[i] * 0.3f;
            float newLevel = qBound(0.1f, baseLevel + wave + random + beatEffect, 1.0f);

            audioLevels[i] = audioLevels[i] * 0.8f + newLevel * 0.2f;

            if (audioLevels[i] > peakLevels[i]) {
                peakLevels[i] = audioLevels[i];
            } else {
                peakLevels[i] = peakLevels[i] * 0.97f;
            }
        }
        visualizerPhase += 0.08f;
        update();
    }

    void updateBeat() {
        if (!mediaLoaded || !isPlaying) return;

        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        if (currentTime - lastBeatTime > 100) {
            if (QRandomGenerator::global()->generate() % 100 < 4) {
                lastBeatTime = currentTime;
                beatIntensity = 0.8f;
                for (int i = 0; i < beatLevels.size(); ++i) {
                    beatLevels[i] = 0.8f;
                }
            }
        }

        beatIntensity *= 0.92f;
        if (beatIntensity < 0.01f) beatIntensity = 0.0f;

        beatPhase += beatIntensity * 0.05f;

        for (int i = 0; i < beatLevels.size(); ++i) {
            beatLevels[i] *= 0.9f;
            if (beatLevels[i] < 0.01f) beatLevels[i] = 0.0f;
        }
    }

    void updateTimeDisplay() {
        if (!mediaLoaded) return;
        qint64 position = player->position();
        qint64 duration = player->duration();
        QString positionTime = formatTime(position);
        QString durationTime = formatTime(duration);
        timeLabel->setText(QString("%1 / %2").arg(positionTime, durationTime));
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

private:
    void loadMediaFile(const QString &fileName) {
        resetPlayer();
        player->setSource(QUrl::fromLocalFile(fileName));
        currentMediaPath = fileName;
        updateFileNameDisplay();
        update();
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
        update();
    }

    void setupUI() {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet(R"(
            QWidget {
                background: transparent;
                border-radius: 8px;
                padding: 5px;
            }
            QPushButton {
                background: rgba(0, 86, 143, 150);
                border: 1px solid rgba(36, 255, 255, 100);
                border-radius: 4px;
                padding: 5px;
            }
            QPushButton:hover {
                background: rgba(0, 86, 143, 200);
                border: 1px solid rgba(36, 255, 255, 200);
            }
            QToolTip {
                color: #24ffff;
                background-color: #333;
                border: 1px solid #555;
                padding: 2px;
            }
            QInputDialog {
                background: rgba(0, 86, 143, 220);
            }
            QMessageBox {
                background: rgba(0, 86, 143, 220);
            }
        )");

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(5);
        mainLayout->setContentsMargins(10, 10, 10, 15);

        // Top bar
        QHBoxLayout *topBarLayout = new QHBoxLayout();
        topBarLayout->addStretch();
        QLabel *apexMusicLabel = new QLabel("ApexMusic v1.03", this);
        apexMusicLabel->setAlignment(Qt::AlignCenter);
        apexMusicLabel->setStyleSheet("QLabel { color: #24ffff; font-size: 12px; font-weight: bold; }");
        topBarLayout->addWidget(apexMusicLabel);
        topBarLayout->addStretch();
        QPushButton *closeButton = new QPushButton(this);
        closeButton->setIcon(QIcon(":/images/close.png"));
        closeButton->setIconSize(QSize(16, 16));
        closeButton->setToolTip("Close");
        closeButton->setStyleSheet("QPushButton { padding: 2px; }");
        connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
        topBarLayout->addWidget(closeButton);
        mainLayout->addLayout(topBarLayout);

        // File name label
        fileNameLabel = new QLabel("No file loaded", this);
        fileNameLabel->setAlignment(Qt::AlignCenter);
        fileNameLabel->setStyleSheet("QLabel { color: #24ffff; font-size: 10px; font-weight: bold; }");
        fileNameLabel->setMaximumWidth(200);
        fileNameLabel->setWordWrap(true);
        mainLayout->addWidget(fileNameLabel);

        // Fixed space for visualizer
        mainLayout->addSpacing(30);

        // Timing label
        timeLabel = new QLabel("0:00 / 0:00", this);
        timeLabel->setAlignment(Qt::AlignCenter);
        timeLabel->setStyleSheet("QLabel { color: #24ffff; font-size: 10px; font-weight: bold; }");
        timeLabel->setToolTip("Current time / Total time");
        mainLayout->addWidget(timeLabel);

        // Control buttons
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        buttonLayout->setSpacing(5);

        QPushButton *backButton = new QPushButton(this);
        backButton->setIcon(QIcon(":/images/back.png"));
        backButton->setIconSize(QSize(24, 24));
        backButton->setToolTip("Back 5 seconds");
        connect(backButton, &QPushButton::clicked, this, [this]() {
            if (mediaLoaded) {
                player->setPosition(player->position() - 5000);
                update();
            }
        });
        connect(backButton, &QPushButton::clicked, this, [this, backButton]() { animateButton(backButton); });
        buttonLayout->addWidget(backButton);

        playButton = new QPushButton(this);
        playButton->setIcon(QIcon(":/images/play.png"));
        playButton->setIconSize(QSize(24, 24));
        playButton->setToolTip("Play/Pause");
        connect(playButton, &QPushButton::clicked, this, &MediaControlWidget::togglePlayPause);
        buttonLayout->addWidget(playButton);

        QPushButton *skipButton = new QPushButton(this);
        skipButton->setIcon(QIcon(":/images/skip.png"));
        skipButton->setIconSize(QSize(24, 24));
        skipButton->setToolTip("Skip 10 seconds");
        connect(skipButton, &QPushButton::clicked, this, &MediaControlWidget::skipForward);
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
    QLabel *timeLabel;
    QLabel *fileNameLabel;
    QTimer *updateTimer;
    QTimer *visualizerTimer;
    QTimer *beatTimer;
    bool mediaLoaded;
    bool isPlaying;
    QString currentMediaPath;
    bool hoverOverProgress;
    bool draggingProgress;
    bool wasPlayingBeforeDrag;
    QRect progressBarRect;
    QList<float> audioLevels;
    QList<float> peakLevels;
    QList<float> beatLevels;
    float visualizerPhase;
    float beatPhase;
    qint64 lastBeatTime;
    float beatIntensity;
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
    ~TrayIcon() {
        delete mediaWidget;
    }

private slots:
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            if (mediaWidget->isVisible()) {
                mediaWidget->hide();
            } else {
                mediaWidget->showControlPanel();
            }
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
