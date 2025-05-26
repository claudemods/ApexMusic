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
        updateTimer->start(50); // Faster update for better beat detection
        visualizerTimer = new QTimer(this);
        connect(visualizerTimer, &QTimer::timeout, this, &MediaControlWidget::updateVisualizer);
        visualizerTimer->start(30); // Faster updates for smoother visualization
        beatTimer = new QTimer(this);
        connect(beatTimer, &QTimer::timeout, this, &MediaControlWidget::updateBeat);
        beatTimer->start(20); // Very fast updates for beat detection
        setMouseTracking(true);
    }
    ~MediaControlWidget() { resetPlayer(); }

    void showControlPanel() {
        // Center the window near the cursor
        QPoint cursorPos = QCursor::pos();
        QRect screenGeometry = QGuiApplication::primaryScreen()->availableGeometry();
        int x = cursorPos.x() - width() / 2;
        int y = cursorPos.y() - height();
        // Ensure the window stays on screen
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
        // Draw background
        painter.fillRect(rect(), QColor(34, 34, 34, 220));
        // Draw audio visualizer
        drawVisualizer(painter);
        // Draw progress bar background
        int progressBarHeight = 2;
        progressBarRect = QRect(10, height() - progressBarHeight - 5, width() - 20, progressBarHeight);
        painter.fillRect(progressBarRect, QColor(60, 60, 60));
        // Draw progress bar if media is loaded
        if (mediaLoaded && player->duration() > 0) {
            double progress = static_cast<double>(player->position()) / player->duration();
            progress = qBound(0.0, progress, 1.0);
            int progressWidth = static_cast<int>(progress * progressBarRect.width());
            QRect progressRect(progressBarRect.x(), progressBarRect.y(), progressWidth, progressBarRect.height());
            // Solid #24ffff color for progress
            painter.fillRect(progressRect, QColor(36, 255, 255));
            // Draw slider handle (always visible during drag, otherwise only on hover)
            if (draggingProgress || hoverOverProgress) {
                int handleSize = 8;
                int handleY = progressBarRect.y() - (handleSize - progressBarHeight) / 2;
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(36, 255, 255));
                painter.drawEllipse(progressRect.right() - handleSize / 2, handleY, handleSize, handleSize);
            }
        }
    }

    void drawVisualizer(QPainter &painter) {
        if (!mediaLoaded)
            return;
        int visualizerHeight = 40; // Increased height for better visualization
        int visualizerWidth = width() - 40;
        int visualizerX = 20;
        int visualizerY = 50;
        // Draw visualizer background
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, 150));
        painter.drawRoundedRect(visualizerX, visualizerY, visualizerWidth, visualizerHeight, 3, 3);
        // Draw audio bars
        int barCount = 30;
        int barWidth = visualizerWidth / barCount;
        int spacing = 2;
        for (int i = 0; i < barCount; ++i) {
            int levelIndex = (i * 2) % audioLevels.size();
            float level = isPlaying ? audioLevels[levelIndex] : 0.1f;
            float peak = isPlaying ? peakLevels[levelIndex] : 0.1f;
            float beat = isPlaying ? beatLevels[levelIndex] : 0.0f;
            // Scale the level to fit the visualizer height with beat enhancement
            int barHeight = qMin(static_cast<int>((level + beat * 0.5) * visualizerHeight * 1.5), visualizerHeight);
            int peakHeight = qMin(static_cast<int>(peak * visualizerHeight * 1.5), visualizerHeight);
            // Calculate position
            int x = visualizerX + i * (barWidth + spacing);
            int y = visualizerY + visualizerHeight - barHeight;
            // Create gradient with beat-responsive colors
            QLinearGradient gradient(x, y, x, y + barHeight);
            // Base color with beat enhancement
            float hue = fmod((i * 12 + beatPhase * 50) / 360.0f, 1.0f);
            float saturation = 0.7f + beat * 0.3f;
            float value = 0.7f + beat * 0.3f;
            QColor baseColor = QColor::fromHsvF(hue, saturation, value);
            QColor peakColor = QColor::fromHsvF(hue, qMin(1.0f, saturation + 0.2f), qMin(1.0f, value + 0.2f));
            gradient.setColorAt(0, baseColor);
            gradient.setColorAt(1, peakColor);
            painter.setBrush(gradient);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(x, y, barWidth, barHeight, 2, 2);
            // Draw peak indicator
            if (peakHeight > barHeight) {
                painter.setBrush(peakColor);
                painter.drawRect(x, y - (peakHeight - barHeight), barWidth, 1);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        // Check if mouse is over progress bar area
        QRect hoverRect(10, height() - 15, width() - 20, 15);
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
        } else {
            player->play();
            playButton->setIcon(QIcon(":/images/pause.png"));
        }
        isPlaying = !isPlaying;
        update();
    }

    void skipForward() {
        if (mediaLoaded) {
            player->setPosition(player->position() + 10000);
            update();
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
            QMessageBox::information(this, "Saved", "Current song added to playlist");
        } else {
            QMessageBox::warning(this, "Error", "Could not save to playlist file");
        }
    }

    void loadPlaylist() {
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
        if (!mediaLoaded)
            return;
        // Generate levels with beat-responsive patterns
        for (int i = 0; i < audioLevels.size(); ++i) {
            // Base level with some randomness
            float baseLevel = isPlaying ? 0.3f : 0.1f;
            float wave = qSin((i + visualizerPhase) * 0.2f) * 0.3f;
            float random = (QRandomGenerator::global()->generate() % 50) / 100.0f;
            // Apply beat effect
            float beatEffect = beatLevels[i] * 0.5f;
            // Calculate new level
            float newLevel = qBound(0.1f, baseLevel + wave * random + beatEffect, 1.0f);
            // Smooth transition
            audioLevels[i] = audioLevels[i] * 0.7f + newLevel * 0.3f;
            // Peak detection
            if (audioLevels[i] > peakLevels[i]) {
                peakLevels[i] = audioLevels[i];
            } else {
                peakLevels[i] = peakLevels[i] * 0.95f; // Slow decay for peaks
            }
        }
        visualizerPhase += 0.1f;
        update();
    }

    void updateBeat() {
        if (!mediaLoaded || !isPlaying)
            return;
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        // Simple beat detection based on timing (more sophisticated would use FFT)
        if (currentTime - lastBeatTime > 100) { // At least 100ms between beats
            // Random beat detection (in a real app, you'd analyze audio here)
            if (QRandomGenerator::global()->generate() % 100 < 5) { // 5% chance of beat
                lastBeatTime = currentTime;
                beatIntensity = 1.0f;
                // Apply beat to all levels
                for (int i = 0; i < beatLevels.size(); ++i) {
                    beatLevels[i] = 1.0f;
                }
            }
        }
        // Decay beat intensity
        beatIntensity *= 0.9f;
        if (beatIntensity < 0.01f)
            beatIntensity = 0.0f;
        // Update beat phase for color animation
        beatPhase += beatIntensity * 0.1f;
        // Decay beat levels
        for (int i = 0; i < beatLevels.size(); ++i) {
            beatLevels[i] *= 0.85f;
            if (beatLevels[i] < 0.01f)
                beatLevels[i] = 0.0f;
        }
    }

    void updateTimeDisplay() {
        if (!mediaLoaded)
            return;
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
                background-color: #222;
                border-radius: 8px;
                padding: 5px;
            }
            QPushButton {
                background: transparent;
                border: none;
                padding: 5px;
            }
            QPushButton:hover {
                background: #444;
                border-radius: 4px;
            }
            QToolTip {
                color: #24ffff;
                background-color: #333;
                border: 1px solid #555;
                padding: 2px;
            }
        )");
        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(5);
        mainLayout->setContentsMargins(10, 10, 10, 15);
        // Top bar with centered title and close button
        QHBoxLayout *topBarLayout = new QHBoxLayout();
        topBarLayout->setContentsMargins(0, 0, 0, 0);
        // Add spacer to center the title
        topBarLayout->addStretch();
        QLabel *apexMusicLabel = new QLabel("ApexMusic v1.02", this);
        apexMusicLabel->setAlignment(Qt::AlignCenter);
        apexMusicLabel->setStyleSheet("QLabel { color: #24ffff; font-size: 12px; font-weight: bold; }");
        topBarLayout->addWidget(apexMusicLabel);
        // Add another spacer and the close button
        topBarLayout->addStretch();
        QPushButton *closeButton = new QPushButton(this);
        closeButton->setIcon(QIcon(":/images/close.png"));
        closeButton->setIconSize(QSize(16, 16));
        closeButton->setToolTip("Close");
        closeButton->setStyleSheet("QPushButton { padding: 2px; }");
        connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
        topBarLayout->addWidget(closeButton);
        mainLayout->addLayout(topBarLayout);
        // File name label (cyan color)
        fileNameLabel = new QLabel("No file loaded", this);
        fileNameLabel->setAlignment(Qt::AlignCenter);
        fileNameLabel->setStyleSheet("QLabel { color: #24ffff; font-size: 10px; }");
        fileNameLabel->setMaximumWidth(200);
        fileNameLabel->setWordWrap(true);
        mainLayout->addWidget(fileNameLabel);
        // Empty space for visualizer (will be drawn in paintEvent)
        mainLayout->addSpacing(50); // More space for the enhanced visualizer
        // Timing label moved below visualizer
        timeLabel = new QLabel("0:00 / 0:00", this);
        timeLabel->setAlignment(Qt::AlignCenter);
        timeLabel->setStyleSheet("QLabel { color: #24ffff; font-size: 10px; }");
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
                update();
            }
        });
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
    // Audio visualization variables
    QList<float> audioLevels;
    QList<float> peakLevels;
    QList<float> beatLevels;
    float visualizerPhase = 0;
    float beatPhase = 0;
    qint64 lastBeatTime = 0;
    float beatIntensity = 0;
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
