/* Copyright 2026 TigerVNC contributors
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __CREDENTIALSTORE_H__
#define __CREDENTIALSTORE_H__

#include <string>
#ifdef WIN32
#include <stdexcept>
#endif

namespace credentialstore {

  // Credentials are persisted only on Windows. The file contents are
  // protected with DPAPI and bound to the current Windows user.
#ifdef WIN32
  class invalid_credential : public std::runtime_error {
  public:
    explicit invalid_credential(const std::string& message)
      : std::runtime_error(message) {}
  };

  std::string serverKey(const std::string& serverName);
#endif
  bool load(const std::string& server, std::string* username,
            std::string* password);
  void save(const std::string& server, const std::string& username,
            const std::string& password);
  void remove(const std::string& server);

}

#endif
