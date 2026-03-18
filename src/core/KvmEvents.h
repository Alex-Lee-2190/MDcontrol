#ifndef MDCONTROL_EVENTS_H
#define MDCONTROL_EVENTS_H

#include <QtCore/QEvent>
#include <QString>
#include <string>
#include "Interfaces.h" 

const QEvent::Type MDControlEvent_ClipboardUpdate = static_cast<QEvent::Type>(QEvent::User + 1);
const QEvent::Type MDControlEvent_SlaveConnected = static_cast<QEvent::Type>(QEvent::User + 2);
const QEvent::Type MDControlEvent_Disconnected = static_cast<QEvent::Type>(QEvent::User + 3);
const QEvent::Type MDControlEvent_FileTransferStart = static_cast<QEvent::Type>(QEvent::User + 4);
const QEvent::Type MDControlEvent_FileTransferProgress = static_cast<QEvent::Type>(QEvent::User + 5);
const QEvent::Type MDControlEvent_FileTransferEnd = static_cast<QEvent::Type>(QEvent::User + 6);
const QEvent::Type MDControlEvent_FileTransferPreparing = static_cast<QEvent::Type>(QEvent::User + 7); 
const QEvent::Type MDControlEvent_FileTransferTraversal = static_cast<QEvent::Type>(QEvent::User + 8); 
const QEvent::Type MDControlEvent_FileTransferCancel = static_cast<QEvent::Type>(QEvent::User + 9); 
const QEvent::Type MDControlEvent_TransferPause = static_cast<QEvent::Type>(QEvent::User + 10); 
const QEvent::Type MDControlEvent_PrepareFileDownload = static_cast<QEvent::Type>(QEvent::User + 11); 
const QEvent::Type MDControlEvent_AuthRequirePin = static_cast<QEvent::Type>(QEvent::User + 12); 

class ClipboardEvent : public QEvent {
public:
    ClipboardEvent(const std::string& text) : QEvent(MDControlEvent_ClipboardUpdate), text(text) {}
    std::string text;
};

class SlaveConnectedEvent : public QEvent {
public:
    SlaveConnectedEvent(InterfaceSocketHandle s, InterfaceSocketHandle fs) : QEvent(MDControlEvent_SlaveConnected), sock(s), fileSock(fs) {}
    InterfaceSocketHandle sock;
    InterfaceSocketHandle fileSock;
};

class DisconnectedEvent : public QEvent {
public:
    DisconnectedEvent() : QEvent(MDControlEvent_Disconnected) {}
};

class FileTransferPreparingEvent : public QEvent {
public:
    FileTransferPreparingEvent(uint32_t tid, const QString& dev, const QString& path) 
        : QEvent(MDControlEvent_FileTransferPreparing), taskId(tid), deviceName(dev), targetPath(path) {}
    uint32_t taskId;
    QString deviceName;
    QString targetPath;
};

class PrepareFileDownloadEvent : public QEvent {
public:
    PrepareFileDownloadEvent() : QEvent(MDControlEvent_PrepareFileDownload) {}
};

class FileTransferTraversalEvent : public QEvent {
public:
    FileTransferTraversalEvent(uint32_t tid, uint64_t currentSize, int currentCount) 
        : QEvent(MDControlEvent_FileTransferTraversal), taskId(tid), totalSize(currentSize), fileCount(currentCount) {}
    uint32_t taskId;
    uint64_t totalSize;
    int fileCount;
};

class FileTransferStartEvent : public QEvent {
public:
    FileTransferStartEvent(uint32_t tid, const QString& dev, const QString& path, uint64_t total, int count) 
        : QEvent(MDControlEvent_FileTransferStart), taskId(tid), deviceName(dev), targetPath(path), totalSize(total), fileCount(count) {}
    uint32_t taskId;
    QString deviceName;
    QString targetPath;
    uint64_t totalSize;
    int fileCount;
};

class FileTransferProgressEvent : public QEvent {
public:
    FileTransferProgressEvent(uint32_t tid, uint64_t current, const std::string& name, int idx) 
        : QEvent(MDControlEvent_FileTransferProgress), taskId(tid), currentBytes(current), fileName(name), currentFileIdx(idx) {}
    uint32_t taskId;
    uint64_t currentBytes;
    std::string fileName;
    int currentFileIdx;
};

class FileTransferEndEvent : public QEvent {
public:
    FileTransferEndEvent(uint32_t tid) : QEvent(MDControlEvent_FileTransferEnd), taskId(tid) {}
    uint32_t taskId;
};

class FileTransferCancelEvent : public QEvent {
public:
    FileTransferCancelEvent(uint32_t tid) : QEvent(MDControlEvent_FileTransferCancel), taskId(tid) {}
    uint32_t taskId;
};

class TransferPauseEvent : public QEvent {
public:
    TransferPauseEvent(uint32_t tid, bool paused) : QEvent(MDControlEvent_TransferPause), taskId(tid), isPaused(paused) {}
    uint32_t taskId;
    bool isPaused;
};

class AuthRequirePinEvent : public QEvent {
public:
    AuthRequirePinEvent(void* promisePtr, const QString& remoteName) 
        : QEvent(MDControlEvent_AuthRequirePin), promise(promisePtr), name(remoteName) {}
    void* promise;
    QString name;
};

#endif // MDCONTROL_EVENTS_H