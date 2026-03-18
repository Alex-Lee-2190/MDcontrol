#include "WinCryptoMgr.h"
#include <windows.h>
#include <wincrypt.h>
#include <vector>

WinCryptoMgr::WinCryptoMgr() {}
WinCryptoMgr::~WinCryptoMgr() {}

void WinCryptoMgr::GenerateRSAKeys(std::string& pubKey, std::string& privKey) {
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return;
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
}

std::string WinCryptoMgr::RSAEncrypt(const std::string& pubKey, const std::string& data) {
    if (pubKey.empty() || data.empty()) return "";

    DWORD blobLen = 0;
    if (!CryptStringToBinaryA(pubKey.c_str(), 0, CRYPT_STRING_BASE64_ANY, NULL, &blobLen, NULL, NULL)) return "";
    std::vector<BYTE> blob(blobLen);
    CryptStringToBinaryA(pubKey.c_str(), 0, CRYPT_STRING_BASE64_ANY, blob.data(), &blobLen, NULL, NULL);

    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return "";
    
    if (!CryptImportKey(hProv, blob.data(), blob.size(), 0, 0, &hKey)) {
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

    return encStr.data();
}

std::string WinCryptoMgr::RSADecrypt(const std::string& privKey, const std::string& encData) {
    if (privKey.empty() || encData.empty()) return "";

    DWORD blobLen = 0;
    if (!CryptStringToBinaryA(privKey.c_str(), 0, CRYPT_STRING_BASE64_ANY, NULL, &blobLen, NULL, NULL)) return "";
    std::vector<BYTE> blob(blobLen);
    CryptStringToBinaryA(privKey.c_str(), 0, CRYPT_STRING_BASE64_ANY, blob.data(), &blobLen, NULL, NULL);

    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return "";
    
    if (!CryptImportKey(hProv, blob.data(), blob.size(), 0, 0, &hKey)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    DWORD dataBlobLen = 0;
    if (!CryptStringToBinaryA(encData.c_str(), 0, CRYPT_STRING_BASE64_ANY, NULL, &dataBlobLen, NULL, NULL) || dataBlobLen == 0) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return "";
    }
    std::vector<BYTE> dataBlob(dataBlobLen);
    CryptStringToBinaryA(encData.c_str(), 0, CRYPT_STRING_BASE64_ANY, dataBlob.data(), &dataBlobLen, NULL, NULL);

    DWORD decLen = dataBlobLen;
    if (!CryptDecrypt(hKey, 0, TRUE, 0, dataBlob.data(), &decLen)) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::string res((char*)dataBlob.data(), decLen);

    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);

    return res;
}

std::string WinCryptoMgr::GenerateRandomString(int length) {
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