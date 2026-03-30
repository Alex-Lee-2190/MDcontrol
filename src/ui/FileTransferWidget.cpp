#include "MainWindow.h"
#include "IconDrawer.h"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QStyle>

static QString FormatBytes(uint64_t size) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double s = static_cast<double>(size);
    while (s >= 1024.0 && unit < 4) {
        s /= 1024.0;
        unit++;
    }
    return QString("%1 %2").arg(s, 0, 'f', 1).arg(units[unit]);
}

// --- FileTransferWidget Implementation ---

FileTransferWidget::FileTransferWidget(uint32_t taskId, const QString& deviceName, const QString& targetPath, uint64_t totalSize, int totalFiles, QWidget* parent) 
    : QWidget(parent, Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint), 
      m_taskId(taskId), m_deviceName(deviceName), m_targetPath(targetPath), m_totalSize(totalSize), m_totalFiles(totalFiles) 
{
    setWindowTitle(T("文件传输 - ") + deviceName);
    setFixedSize(450, 360); 
    
    m_finished = false;

    QVBoxLayout* layout = new QVBoxLayout(this);
    
    QHBoxLayout* topLayout = new QHBoxLayout();
    m_lblPercentage = new QLabel(T("已完成 0%"));
    QFont f = font();
    f.setPointSize(14);
    f.setBold(true);
    m_lblPercentage->setFont(f);
    
    m_btnCancel = new QPushButton();
    m_btnCancel->setIcon(IconDrawer::getDeleteIcon());
    m_btnCancel->setIconSize(QSize(24, 24));
    m_btnCancel->setStyleSheet("QPushButton { background: transparent; border: none; } QPushButton:hover { background: rgba(128, 128, 128, 0.2); border-radius: 4px; }");
    m_btnCancel->setFixedSize(30, 30);
    connect(m_btnCancel, &QPushButton::clicked, this, &QWidget::close);
    
    m_btnPause = new QPushButton();
    m_btnPause->setIcon(IconDrawer::getTransferPauseIcon(false));
    m_btnPause->setIconSize(QSize(24, 24));
    m_btnPause->setCheckable(true);
    m_btnPause->setFixedSize(30, 30);
    m_btnPause->setToolTip(T("暂停/继续"));
    m_btnPause->setStyleSheet("QPushButton { background: transparent; border: none; } QPushButton:hover { background: rgba(128, 128, 128, 0.2); border-radius: 4px; }");
    connect(m_btnPause, &QPushButton::toggled, this, &FileTransferWidget::onPauseClicked);

    topLayout->addWidget(m_lblPercentage);
    topLayout->addStretch();
    topLayout->addWidget(m_btnPause); 
    topLayout->addWidget(m_btnCancel);
    layout->addLayout(topLayout);
    
    m_graph = new SpeedGraphWidget(this);
    layout->addWidget(m_graph);
    
    QVBoxLayout* detailsLayout = new QVBoxLayout();
    
    m_lblDevice = new QLabel(T("目标设备: %1").arg(m_deviceName));
    m_lblDevice->setStyleSheet("color: #666; font-weight: bold;");
    m_lblPath = new QLabel(T("保存至: %1").arg(m_targetPath));
    m_lblPath->setWordWrap(true); 
    
    m_lblFileName = new QLabel(T("名称: "));
    m_lblTimeRemaining = new QLabel(T("剩余时间: 计算中..."));
    
    QString sizeStr = FormatBytes(totalSize);
    m_lblItemsRemaining = new QLabel(T("剩余项目: %1 (%2)").arg(totalFiles).arg(sizeStr));
    
    detailsLayout->addWidget(m_lblDevice);
    detailsLayout->addWidget(m_lblPath);
    detailsLayout->addWidget(m_lblFileName);
    detailsLayout->addWidget(m_lblTimeRemaining);
    detailsLayout->addWidget(m_lblItemsRemaining);
    layout->addLayout(detailsLayout);
}

void FileTransferWidget::updateTheme() {
    m_btnCancel->setIcon(IconDrawer::getDeleteIcon());
    m_btnPause->setIcon(IconDrawer::getTransferPauseIcon(m_btnPause->isChecked()));
}

void FileTransferWidget::setTotalInfo(uint64_t totalSize, int totalFiles) {
    m_totalSize = totalSize;
    m_totalFiles = totalFiles;
    QString sizeStr = FormatBytes(totalSize);
    m_lblItemsRemaining->setText(T("剩余项目: %1 (%2)").arg(totalFiles).arg(sizeStr));
}

void FileTransferWidget::updateProgress(uint64_t currentBytes, double speed, const std::string& currentFile, int currentIdx) {
    int percent = 0;
    if (m_totalSize > 0) percent = (int)((double)currentBytes / m_totalSize * 100.0);
    else if (m_totalFiles > 0) percent = (int)((double)currentIdx / m_totalFiles * 100.0);
    if (percent > 100) percent = 100; 
    m_lblPercentage->setText(T("已完成 %1%").arg(percent));
    
    m_graph->addSpeedSample(speed);
    QString speedStr = FormatBytes((uint64_t)speed) + "/s";
    m_graph->setCurrentSpeedText(T("速度: %1").arg(speedStr));
    
    m_lblFileName->setText(T("名称: %1").arg(QString::fromStdString(currentFile)));
    
    if (speed > 0) {
        uint64_t bytesLeft = (currentBytes >= m_totalSize) ? 0 : (m_totalSize - currentBytes);
        double seconds = (double)bytesLeft / speed;
        int s = (int)seconds;
        if (s < 60) m_lblTimeRemaining->setText(T("剩余时间: 大约 %1 秒").arg(s));
        else m_lblTimeRemaining->setText(T("剩余时间: 大约 %1 分 %2 秒").arg(s/60).arg(s%60));
    }
    
    int filesLeft = m_totalFiles - currentIdx;
    uint64_t bytesLeft = (currentBytes >= m_totalSize) ? 0 : (m_totalSize - currentBytes);
    QString sizeStr = FormatBytes(bytesLeft);
    m_lblItemsRemaining->setText(T("剩余项目: %1 (%2)").arg(filesLeft).arg(sizeStr));
}

void FileTransferWidget::updateTraversalProgress(uint64_t currentSize, int currentFiles) {
    m_lblPercentage->setText(T("正在统计元数据..."));
    m_lblFileName->setText(T("正在扫描文件..."));
    m_lblTimeRemaining->setText(T("剩余时间: 统计中..."));
    
    QString sizeStr = FormatBytes(currentSize);
    m_lblItemsRemaining->setText(T("已扫描: %1 (%2)").arg(currentFiles).arg(sizeStr));
}

void FileTransferWidget::setPaused(bool paused) {
    QSignalBlocker blocker(m_btnPause);
    m_btnPause->setChecked(paused);
    m_btnPause->setIcon(IconDrawer::getTransferPauseIcon(paused));
}

void FileTransferWidget::onPauseClicked(bool checked) {
    m_btnPause->setIcon(IconDrawer::getTransferPauseIcon(checked));
    emit pauseToggled(checked);
}

void FileTransferWidget::setFinished() {
    m_finished = true;
    close();
}

void FileTransferWidget::closeEvent(QCloseEvent* event) {
    if (!m_finished) {
        emit cancelled();
    }
    QWidget::closeEvent(event);
}