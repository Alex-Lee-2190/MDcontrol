#ifndef MASTER_INTERNAL_H
#define MASTER_INTERNAL_H

#include <memory>
#include <vector>
#include "Common.h"

void FileReceiverThreadFunc(std::shared_ptr<SlaveCtx> ctx);
void MasterSendFileThread(std::shared_ptr<SlaveCtx> ctx, uint32_t taskId, std::vector<FileClipInfo> localRoots);


#endif // MASTER_INTERNAL_H