#ifndef WIN_CRYPTO_MGR_H
#define WIN_CRYPTO_MGR_H

#include "Interfaces.h"
#include <string>

class WinCryptoMgr : public ICryptoMgr {
public:
    WinCryptoMgr();
    virtual ~WinCryptoMgr();

    void GenerateRSAKeys(std::string& pubKey, std::string& privKey) override;
    std::string RSAEncrypt(const std::string& pubKey, const std::string& data) override;
    std::string RSADecrypt(const std::string& privKey, const std::string& encData) override;
    std::string GenerateRandomString(int length) override;
    std::string GetPublicKeyFingerprint(const std::string& pubKey) override;
};

#endif