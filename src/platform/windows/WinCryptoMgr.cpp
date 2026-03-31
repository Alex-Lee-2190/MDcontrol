#include "WinCryptoMgr.h"
#include "Common.h"
#include "SystemUtils.h"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

WinCryptoMgr::WinCryptoMgr() {}
WinCryptoMgr::~WinCryptoMgr() {}

void WinCryptoMgr::GenerateRSAKeys(std::string& pubKey, std::string& privKey) {
    uint32_t startMs = SystemUtils::GetTimeMS();
    MDC_LOG_INFO(LogTag::AUTH, "GenerateRSAKeys started");
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptAcquireContextW failed error: %lu", GetLastError());
        return;
    }
    CryptGenKey(hProv, AT_KEYEXCHANGE, (1024 << 16) | CRYPT_EXPORTABLE, &hKey);

    DWORD dwLen = 0;
    CryptExportKey(hKey, 0, PUBLICKEYBLOB, 0, NULL, &dwLen);
    std::vector<BYTE> pubBlob(dwLen);
    CryptExportKey(hKey, 0, PUBLICKEYBLOB, 0, pubBlob.data(), &dwLen);

    dwLen = 0;
    CryptExportKey(hKey, 0, PRIVATEKEYBLOB, 0, NULL, &dwLen);
    std::vector<BYTE> privBlob(dwLen);
    CryptExportKey(hKey, 0, PRIVATEKEYBLOB, 0, privBlob.data(), &dwLen);

    DWORD strLen = 0;
    CryptBinaryToStringA(pubBlob.data(), pubBlob.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &strLen);
    std::vector<char> pubStr(strLen);
    CryptBinaryToStringA(pubBlob.data(), pubBlob.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, pubStr.data(), &strLen);
    pubKey = pubStr.data();

    strLen = 0;
    CryptBinaryToStringA(privBlob.data(), privBlob.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &strLen);
    std::vector<char> privStr(strLen);
    CryptBinaryToStringA(privBlob.data(), privBlob.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, privStr.data(), &strLen);
    privKey = privStr.data();

    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);
    MDC_LOG_INFO(LogTag::AUTH, "GenerateRSAKeys completed in %u ms pubLen: %zu privLen: %zu", SystemUtils::GetTimeMS() - startMs, pubKey.length(), privKey.length());
}

std::string WinCryptoMgr::RSAEncrypt(const std::string& pubKey, const std::string& data) {
    if (pubKey.empty() || data.empty()) return "";
    uint32_t startMs = SystemUtils::GetTimeMS();
    MDC_LOG_TRACE(LogTag::AUTH, "RSAEncrypt started input len: %zu", data.length());

    DWORD blobLen = 0;
    if (!CryptStringToBinaryA(pubKey.c_str(), 0, CRYPT_STRING_BASE64_ANY, NULL, &blobLen, NULL, NULL)) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptStringToBinaryA failed error: %lu", GetLastError());
        return "";
    }
    std::vector<BYTE> blob(blobLen);
    CryptStringToBinaryA(pubKey.c_str(), 0, CRYPT_STRING_BASE64_ANY, blob.data(), &blobLen, NULL, NULL);

    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptAcquireContextW failed error: %lu", GetLastError());
        return "";
    }
    
    if (!CryptImportKey(hProv, blob.data(), blob.size(), 0, 0, &hKey)) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptImportKey failed error: %lu", GetLastError());
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::vector<BYTE> buf(256); 
    DWORD dataLen = data.length();
    if (dataLen > 200) { 
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    memcpy(buf.data(), data.data(), dataLen);

    if (!CryptEncrypt(hKey, 0, TRUE, 0, buf.data(), &dataLen, buf.size())) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptEncrypt failed error: %lu", GetLastError());
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    DWORD strLen = 0;
    CryptBinaryToStringA(buf.data(), dataLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &strLen);
    std::vector<char> encStr(strLen);
    CryptBinaryToStringA(buf.data(), dataLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encStr.data(), &strLen);

    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);

    MDC_LOG_TRACE(LogTag::AUTH, "RSAEncrypt completed in %u ms output len: %zu", SystemUtils::GetTimeMS() - startMs, encStr.size() - 1);
    return encStr.data();
}

std::string WinCryptoMgr::RSADecrypt(const std::string& privKey, const std::string& encData) {
    if (privKey.empty() || encData.empty()) return "";
    uint32_t startMs = SystemUtils::GetTimeMS();
    MDC_LOG_TRACE(LogTag::AUTH, "RSADecrypt started input len: %zu", encData.length());

    DWORD blobLen = 0;
    if (!CryptStringToBinaryA(privKey.c_str(), 0, CRYPT_STRING_BASE64_ANY, NULL, &blobLen, NULL, NULL)) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptStringToBinaryA failed error: %lu", GetLastError());
        return "";
    }
    std::vector<BYTE> blob(blobLen);
    CryptStringToBinaryA(privKey.c_str(), 0, CRYPT_STRING_BASE64_ANY, blob.data(), &blobLen, NULL, NULL);

    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptAcquireContextW failed error: %lu", GetLastError());
        return "";
    }
    
    if (!CryptImportKey(hProv, blob.data(), blob.size(), 0, 0, &hKey)) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptImportKey failed error: %lu", GetLastError());
        CryptReleaseContext(hProv, 0);
        return "";
    }

    DWORD dataBlobLen = 0;
    if (!CryptStringToBinaryA(encData.c_str(), 0, CRYPT_STRING_BASE64_ANY, NULL, &dataBlobLen, NULL, NULL) || dataBlobLen == 0) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptStringToBinaryA failed for data error: %lu", GetLastError());
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    std::vector<BYTE> dataBlob(dataBlobLen);
    CryptStringToBinaryA(encData.c_str(), 0, CRYPT_STRING_BASE64_ANY, dataBlob.data(), &dataBlobLen, NULL, NULL);

    DWORD decLen = dataBlobLen;
    if (!CryptDecrypt(hKey, 0, TRUE, 0, dataBlob.data(), &decLen)) {
        MDC_LOG_ERROR(LogTag::AUTH, "CryptDecrypt failed error: %lu", GetLastError());
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::string res((char*)dataBlob.data(), decLen);

    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);

    MDC_LOG_TRACE(LogTag::AUTH, "RSADecrypt completed in %u ms output len: %zu", SystemUtils::GetTimeMS() - startMs, res.length());
    return res;
}

std::string WinCryptoMgr::GenerateRandomString(int length) {
    MDC_LOG_TRACE(LogTag::AUTH, "GenerateRandomString requested length: %d", length);
    if (length <= 0) return "";
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return "";
    
    std::vector<BYTE> buf(length);
    CryptGenRandom(hProv, length, buf.data());
    CryptReleaseContext(hProv, 0);

    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string res;
    for (int i = 0; i < length; ++i) {
        res += charset[buf[i] % (sizeof(charset) - 1)];
    }
    return res;
}

std::string WinCryptoMgr::GetPublicKeyFingerprint(const std::string& pubKey) {
    if (pubKey.empty()) return "";
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status != 0) return "";

    DWORD cbHashObject = 0, cbData = 0, cbResult = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbData, sizeof(DWORD), &cbResult, 0);
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, sizeof(DWORD), &cbResult, 0);

    std::vector<BYTE> hashObject(cbHashObject);
    std::vector<BYTE> hash(cbData);

    status = BCryptCreateHash(hAlg, &hHash, hashObject.data(), cbHashObject, NULL, 0, 0);
    status = BCryptHashData(hHash, (PBYTE)pubKey.c_str(), (ULONG)pubKey.length(), 0);
    status = BCryptFinishHash(hHash, hash.data(), (ULONG)hash.size(), 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::string res;
    char hexBuf[3];
    for (DWORD i = 0; i < cbData; i++) {
        sprintf(hexBuf, "%02x", hash[i]);
        res += hexBuf;
    }
    return res;
}