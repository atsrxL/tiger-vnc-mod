/* Copyright 2026 TigerVNC contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CredentialStore.h"

#ifdef WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <dpapi.h>
#include <io.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <vector>

#include <core/Exception.h>
#include <core/string.h>
#include <core/xdgdirs.h>

#include <network/TcpSocket.h>

namespace {

const uint8_t CREDENTIAL_MAGIC[] = {
  'T', 'V', 'N', 'C', 'C', 'R', 'E', 'D', 1
};
const long MAX_CREDENTIAL_FILE_SIZE = 1024 * 1024;

class LocalDataBlob
{
public:
  explicit LocalDataBlob(bool wipe_) : wipe(wipe_)
  {
    blob.cbData = 0;
    blob.pbData = nullptr;
  }

  ~LocalDataBlob()
  {
    if (blob.pbData == nullptr)
      return;
    if (wipe)
      SecureZeroMemory(blob.pbData, blob.cbData);
    LocalFree(blob.pbData);
  }

  DATA_BLOB* output()
  {
    return &blob;
  }

  DATA_BLOB blob;

private:
  LocalDataBlob(const LocalDataBlob&) = delete;
  LocalDataBlob& operator=(const LocalDataBlob&) = delete;

  bool wipe;
};

class SecureBytes
{
public:
  SecureBytes() = default;

  ~SecureBytes()
  {
    if (!data.empty())
      SecureZeroMemory(data.data(), data.size());
  }

  std::vector<uint8_t> data;

private:
  SecureBytes(const SecureBytes&) = delete;
  SecureBytes& operator=(const SecureBytes&) = delete;
};

uint64_t serverHash(const std::string& server)
{
  uint64_t hash = UINT64_C(14695981039346656037);

  for (unsigned char ch : server) {
    hash ^= ch;
    hash *= UINT64_C(1099511628211);
  }

  return hash;
}

std::string credentialDirectory(bool create)
{
  const char* configDir = core::getvncconfigdir();
  if (configDir == nullptr)
    throw std::runtime_error("Could not determine VNC config directory path");

  std::string directory = core::format("%s/credentials", configDir);

  if (create && (core::mkdir_p(directory.c_str(), 0700) == -1) &&
      (errno != EEXIST)) {
    throw core::posix_error(
      core::format("Failed to create credential directory \"%s\"",
                   directory.c_str()),
      errno);
  }

  return directory;
}

std::string credentialPath(const std::string& server, bool createDirectory)
{
  return core::format("%s/%016llx.credential",
                      credentialDirectory(createDirectory).c_str(),
                      (unsigned long long)serverHash(server));
}

void appendUint32(std::vector<uint8_t>* data, uint32_t value)
{
  data->push_back((uint8_t)(value & 0xff));
  data->push_back((uint8_t)((value >> 8) & 0xff));
  data->push_back((uint8_t)((value >> 16) & 0xff));
  data->push_back((uint8_t)((value >> 24) & 0xff));
}

void appendString(std::vector<uint8_t>* data, const std::string& value)
{
  if (value.size() > (std::numeric_limits<uint32_t>::max)())
    throw std::length_error("Credential value is too large");

  appendUint32(data, (uint32_t)value.size());
  data->insert(data->end(), value.begin(), value.end());
}

bool readUint32(const uint8_t* data, size_t size, size_t* position,
                uint32_t* value)
{
  if (*position > size || (size - *position) < 4)
    return false;

  *value = (uint32_t)data[*position] |
           ((uint32_t)data[*position + 1] << 8) |
           ((uint32_t)data[*position + 2] << 16) |
           ((uint32_t)data[*position + 3] << 24);
  *position += 4;
  return true;
}

bool readString(const uint8_t* data, size_t size, size_t* position,
                std::string* value)
{
  uint32_t length;

  if (!readUint32(data, size, position, &length))
    return false;
  if (*position > size || length > (size - *position))
    return false;

  value->assign((const char*)data + *position, length);
  *position += length;
  return true;
}

bool readEncryptedFile(const std::string& path, std::vector<uint8_t>* data)
{
  FILE* file = fopen(path.c_str(), "rb");
  if (file == nullptr) {
    if (errno == ENOENT)
      return false;
    throw core::posix_error(
      core::format("Failed to open \"%s\"", path.c_str()), errno);
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    int err = errno;
    fclose(file);
    throw core::posix_error(
      core::format("Failed to read \"%s\"", path.c_str()), err);
  }

  long size = ftell(file);
  if (size < 0 || size > MAX_CREDENTIAL_FILE_SIZE) {
    fclose(file);
    throw std::runtime_error("Stored credential has an invalid size");
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    int err = errno;
    fclose(file);
    throw core::posix_error(
      core::format("Failed to read \"%s\"", path.c_str()), err);
  }

  data->resize((size_t)size);
  if (!data->empty() &&
      fread(data->data(), 1, data->size(), file) != data->size()) {
    int err = ferror(file) ? errno : EIO;
    fclose(file);
    throw core::posix_error(
      core::format("Failed to read \"%s\"", path.c_str()), err);
  }

  if (fclose(file) != 0)
    throw core::posix_error(
      core::format("Failed to close \"%s\"", path.c_str()), errno);

  return true;
}

void writeEncryptedFile(const std::string& path,
                        const uint8_t* data, size_t size)
{
  std::string temporaryPath = core::format(
    "%s.tmp.%lu", path.c_str(), (unsigned long)GetCurrentProcessId());

  FILE* file = fopen(temporaryPath.c_str(), "wb");
  if (file == nullptr)
    throw core::posix_error(
      core::format("Failed to open \"%s\"", temporaryPath.c_str()), errno);

  if ((size != 0) && (fwrite(data, 1, size, file) != size)) {
    int err = errno ? errno : EIO;
    fclose(file);
    DeleteFileA(temporaryPath.c_str());
    throw core::posix_error(
      core::format("Failed to write \"%s\"", temporaryPath.c_str()), err);
  }

  if ((fflush(file) != 0) || (_commit(_fileno(file)) != 0)) {
    int err = errno;
    fclose(file);
    DeleteFileA(temporaryPath.c_str());
    throw core::posix_error(
      core::format("Failed to write \"%s\"", temporaryPath.c_str()), err);
  }

  if (fclose(file) != 0) {
    int err = errno;
    DeleteFileA(temporaryPath.c_str());
    throw core::posix_error(
      core::format("Failed to close \"%s\"", temporaryPath.c_str()), err);
  }

  if (!MoveFileExA(temporaryPath.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DWORD err = GetLastError();
    DeleteFileA(temporaryPath.c_str());
    throw core::win32_error(
      core::format("Failed to replace \"%s\"", path.c_str()), err);
  }
}

}

std::string credentialstore::serverKey(const std::string& serverName)
{
  std::string host;
  int port;

  network::getHostAndPort(serverName.c_str(), &host, &port);
  std::transform(host.begin(), host.end(), host.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });

  return core::format("server:%s:%d", host.c_str(), port);
}

bool credentialstore::load(const std::string& server, std::string* username,
                           std::string* password)
{
  std::vector<uint8_t> encrypted;
  if (!readEncryptedFile(credentialPath(server, false), &encrypted))
    return false;
  if (encrypted.empty())
    throw credentialstore::invalid_credential("Stored credential is empty");
  if (encrypted.size() > (std::numeric_limits<DWORD>::max)())
    throw credentialstore::invalid_credential(
      "Stored credential is too large");

  DATA_BLOB encryptedBlob;
  encryptedBlob.cbData = (DWORD)encrypted.size();
  encryptedBlob.pbData = encrypted.data();

  LocalDataBlob plainBlob(true);
  if (!CryptUnprotectData(&encryptedBlob, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN,
                          plainBlob.output())) {
    DWORD err = GetLastError();
    if (err == ERROR_INVALID_DATA)
      throw credentialstore::invalid_credential(
        "Stored credential could not be decrypted");
    throw core::win32_error("CryptUnprotectData", err);
  }

  bool valid = plainBlob.blob.cbData >= sizeof(CREDENTIAL_MAGIC) &&
               memcmp(plainBlob.blob.pbData, CREDENTIAL_MAGIC,
                      sizeof(CREDENTIAL_MAGIC)) == 0;
  size_t position = sizeof(CREDENTIAL_MAGIC);
  std::string storedServer;
  std::string storedUsername;
  std::string storedPassword;

  if (valid)
    valid = readString(plainBlob.blob.pbData, plainBlob.blob.cbData, &position,
                       &storedServer);
  if (valid)
    valid = readString(plainBlob.blob.pbData, plainBlob.blob.cbData, &position,
                       &storedUsername);
  if (valid)
    valid = readString(plainBlob.blob.pbData, plainBlob.blob.cbData, &position,
                       &storedPassword);
  if (valid)
    valid = position == plainBlob.blob.cbData;

  if (!valid)
    throw credentialstore::invalid_credential(
      "Stored credential has an invalid format");
  if (storedServer != server)
    throw std::runtime_error("Stored credential belongs to another server");

  *username = storedUsername;
  *password = storedPassword;
  if (!storedPassword.empty())
    SecureZeroMemory(&storedPassword[0], storedPassword.size());
  return true;
}

void credentialstore::save(const std::string& server,
                           const std::string& username,
                           const std::string& password)
{
  SecureBytes plain;
  plain.data.insert(plain.data.end(), CREDENTIAL_MAGIC,
                    CREDENTIAL_MAGIC + sizeof(CREDENTIAL_MAGIC));
  appendString(&plain.data, server);
  appendString(&plain.data, username);
  appendString(&plain.data, password);

  if (plain.data.size() > (std::numeric_limits<DWORD>::max)())
    throw std::runtime_error("Credential is too large");

  DATA_BLOB plainBlob;
  plainBlob.cbData = (DWORD)plain.data.size();
  plainBlob.pbData = plain.data.data();

  LocalDataBlob encryptedBlob(false);
  if (!CryptProtectData(&plainBlob, L"TigerVNC saved credential", nullptr,
                        nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                        encryptedBlob.output())) {
    DWORD err = GetLastError();
    throw core::win32_error("CryptProtectData", err);
  }

  std::string path = credentialPath(server, true);
  writeEncryptedFile(path, encryptedBlob.blob.pbData,
                     encryptedBlob.blob.cbData);
}

void credentialstore::remove(const std::string& server)
{
  std::string path = credentialPath(server, false);
  if (DeleteFileA(path.c_str()))
    return;

  DWORD err = GetLastError();
  if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
    return;
  throw core::win32_error(
    core::format("Failed to remove \"%s\"", path.c_str()), err);
}

#else

bool credentialstore::load(const std::string&, std::string*, std::string*)
{
  return false;
}

void credentialstore::save(const std::string&, const std::string&,
                           const std::string&)
{
}

void credentialstore::remove(const std::string&)
{
}

#endif
