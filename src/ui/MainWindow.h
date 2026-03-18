#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtWidgets/QWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget> 
#include <QtWidgets/QDialog>
#include <QtWidgets/QCheckBox>
#include <QtGui/QMouseEvent> 
#include <QtGui/QWheelEvent>
#include <QtGui/QKeyEvent>
#include <QtCore/QByteArray>
#include <QtCore/QSettings> 
#include <QtWidgets/QProgressDialog>
#include <QtCore/QPointer>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QSystemTrayIcon>
#include <QtWidgets/QMenu>
#include <map>
#include <set>
#include <QtCore/QAbstractListModel>
#include <QtWidgets/QStyledItemDelegate>
#include "Common.h" 
#include "KvmEvents.h"

QString T(const QString& zh);

struct ConflictItem {
    int idx;
    QString fileName;
    uint64_t sourceSize;
    uint64_t sourceTime;
    uint64_t targetSize;
    uint64_t targetTime;
    bool keepSource; 
};

class ConflictListModel : public QAbstractListModel {
    Q_OBJECT
public:
    ConflictListModel(const std::vector<ConflictItem>& items, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    
    void setAllKeepSource(bool keepSource);
    std::vector<ConflictItem> getItems() const { return m_items; }

private:
    std::vector<ConflictItem> m_items;
};

class ConflictItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    ConflictItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;
};

class DuplicateDecisionDialog : public QDialog {
    Q_OBJECT
public:
    DuplicateDecisionDialog(const std::vector<ConflictItem>& items, QWidget* parent = nullptr);
    std::set<int> getSkippedIndices() const { return m_skippedIndices; }
private:
    std::set<int> m_skippedIndices;
    ConflictListModel* m_model;
};

class MapWidget : public QWidget {
public:
    explicit MapWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override; 
    void keyPressEvent(QKeyEvent* event) override; 

private:
    bool m_isDragging = false;
    int m_dragSlaveIdx = -1; 
    QPoint m_dragStartPos;
    QPointF m_dragStartRemotePos; 
    
    QPointF m_viewOffset = {0.0, 0.0};
    double m_zoomFactor = 1.0;          
    bool m_isPanning = false;
    QPoint m_panStartPos;
    QPointF m_panStartOffset;
    
    QRectF m_currentLocalRect;
    std::vector<QRectF> m_currentSlaveRects; 

    int m_selectedDeviceIdx = -1;
    
    QRectF m_barRect;
    QRectF m_settingsBtnRect;
    QRectF m_addBtnRect;
    QRectF m_lockBtnRect;
    QRectF m_pauseBtnRect;
    QRectF m_reconBtnRect;
    QRectF m_wifiBtnRect; 
    QRectF m_stopBtnRect;
    
    QRectF m_latencyRect;
    QRectF m_focusBtnRect;
    QRectF m_zoomInBtnRect;  
    QRectF m_zoomOutBtnRect; 

    int m_pressedBtn = 0; 
    int m_hoveredBtn = 0;
};

class SpeedGraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpeedGraphWidget(QWidget* parent = nullptr);
    void addSpeedSample(double bytesPerSec);
    void setCurrentSpeedText(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<double> m_samples;
    double m_maxSpeed;
    QString m_speedText;
};

class FileTransferWidget : public QWidget {
    Q_OBJECT
public:
    FileTransferWidget(uint32_t taskId, const QString& deviceName, const QString& targetPath, uint64_t totalSize, int totalFiles, QWidget* parent = nullptr);
    void updateProgress(uint64_t currentBytes, double speed, const std::string& currentFile, int currentIdx);
    void updateTraversalProgress(uint64_t currentSize, int currentFiles); 
    void setTotalInfo(uint64_t totalSize, int totalFiles); 
    void setPaused(bool paused);
    void setFinished();
    uint32_t getTaskId() const { return m_taskId; }
    
    bool hasShownBtWarning() const { return m_btWarningShown; }
    void setBtWarningShown(bool shown = true) { m_btWarningShown = shown; }

signals:
    void cancelled();
    void pauseToggled(bool paused);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onPauseClicked(bool checked);

private:
    uint32_t m_taskId;
    QString m_deviceName;
    QString m_targetPath;
    uint64_t m_totalSize;
    int m_totalFiles;
    
    QLabel* m_lblPercentage;
    QLabel* m_lblFileName;
    QLabel* m_lblTimeRemaining;
    QLabel* m_lblItemsRemaining;
    QLabel* m_lblDevice; 
    QLabel* m_lblPath;   
    
    SpeedGraphWidget* m_graph;
    QPushButton* m_btnCancel;
    QPushButton* m_btnPause;
    
    bool m_finished = false;
    bool m_btWarningShown = false; 
};

struct TransferStats {
    uint64_t lastBytes = 0;
    uint32_t lastTime = 0;
    double currentSpeed = 0.0;
    uint32_t lastUiUpdate = 0;
};

class ControlWindow : public QWidget {
public:
    QLineEdit* ipEdit;
    QLineEdit* portEdit;
    QRadioButton* modeTcpBtn; 
    QRadioButton* modeBtBtn;  
    QWidget* tcpConfigWidget; 
    QPushButton* startBtn; 
    QLabel* statusLabel;
    MapWidget* mapWidget;
    QListWidget* btListWidget; 
    
    QCheckBox* listenTcpCb;
    QCheckBox* listenBtCb;
    QLineEdit* listenPortEdit;

    QLabel* m_lblConnectAsMaster;
    QLabel* m_lblIp;
    QLabel* m_lblPort;
    QLabel* m_lblListenAsMaster;

    std::map<uint32_t, QPointer<FileTransferWidget>> m_transferWidgets; 
    std::map<uint32_t, TransferStats> m_transferStats;

    QWidget* m_connectWindow; 
    
    std::map<unsigned long long, QString> m_scannedBtDevices; 
    
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_trayMenu;

    ControlWindow();

    void onStart(); 
    void onStop();  
    void onModeChanged(int index); 
    void onBtItemClicked(QListWidgetItem* item); 
    void startMasterCommon(SOCKET newSock, std::string name);
    void openSettings(); 
    void showConnectWindow(); 
    
    void startListening();
    void stopListening();
    void refreshBtList(); 

    void updateLanguageUI(); 

    void onTransferCancelled(uint32_t taskId); 
    void onTransferPaused(uint32_t taskId, bool paused);
    
    void reconnectSlave(int idx);

    void updateTrayMenu();

protected:
    void customEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
};

#endif