/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2012 Samuel Mannehed <samuel@cendio.se> for Cendio AB
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <algorithm>
#include <libgen.h>

// FIXME: Workaround for FLTK including windows.h
#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>

#include <core/Exception.h>
#include <core/LogWriter.h>
#include <core/i18n.h>
#include <core/string.h>
#include <core/xdgdirs.h>

#include <network/TcpSocket.h>

#include "fltk/layout.h"
#include "fltk/util.h"
#include "fltk/Fl_Suggestion_Input.h"
#ifdef WIN32
#include "CConn.h"
#endif
#include "ServerDialog.h"
#include "OptionsDialog.h"
#include "vncviewer.h"
#include "parameters.h"

static core::LogWriter vlog("ServerDialog");

const char* SERVER_HISTORY="tigervnc.history";

ServerDialog::ServerDialog()
  : Fl_Window(450, 0, "TigerVNC")
{
  int x, y, x2;
  Fl_Button *button;
  Fl_Box *divider;

  x = OUTER_MARGIN;
  y = OUTER_MARGIN;

  serverName = new Fl_Suggestion_Input(
    LBLLEFT(x, y, w() - OUTER_MARGIN*2, INPUT_HEIGHT, _("VNC server:")), {}
  );
  serverName->call_on_remove(onServerHistoryRemove, this);
  serverName->call_to_normalize(serverHistoryNormalize);

  y += INPUT_HEIGHT + INNER_MARGIN;

  x2 = x;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Options..."));
  button->callback(this->handleOptions, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Load..."));
  button->callback(this->handleLoad, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Save as..."));
  button->callback(this->handleSaveAs, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  y += BUTTON_HEIGHT + INNER_MARGIN;

#ifdef WIN32
  button = new Fl_Button(x, y, BUTTON_WIDTH * 2 + INNER_MARGIN,
                         BUTTON_HEIGHT, _("Forget saved password"));
  button->callback(this->handleForgetPassword, this);
  y += BUTTON_HEIGHT + INNER_MARGIN;
#endif

  divider = new Fl_Box(0, y, w(), 2);
  divider->box(FL_THIN_DOWN_FRAME);

  y += divider->h() + INNER_MARGIN;

  // Symmetric margin around bottom button bar
  y += OUTER_MARGIN - INNER_MARGIN;

  button = new Fl_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("About..."));
  button->callback(this->handleAbout, this);

  x2 = w() - OUTER_MARGIN - BUTTON_WIDTH*2 - INNER_MARGIN*1;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Cancel"));
  button->callback(this->handleCancel, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Return_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Connect"));
  button->callback(this->handleConnect, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  y += BUTTON_HEIGHT + INNER_MARGIN;

  /* Needed for resize to work sanely */
  resizable(nullptr);
  h(y-INNER_MARGIN+OUTER_MARGIN);

  callback(this->handleCancel, this);
}


ServerDialog::~ServerDialog()
{
}


void ServerDialog::run(const char* servername, char *newservername)
{
  ServerDialog dialog;

  dialog.serverName->value(servername);

  dialog.show();

  try {
    dialog.loadServerHistory();
    dialog.serverName->set_suggestions(dialog.serverHistory);
  } catch (std::exception& e) {
    vlog.error(_("Unable to load the server history: %s"), e.what());
  }

  while (dialog.shown()) Fl::wait();

  if (dialog.serverName->value() == nullptr) {
    newservername[0] = '\0';
    return;
  }

  strncpy(newservername, dialog.serverName->value(), VNCSERVERNAMELEN);
  newservername[VNCSERVERNAMELEN - 1] = '\0';
}

void ServerDialog::handleOptions(Fl_Widget* /*widget*/, void* /*data*/)
{
  OptionsDialog::showDialog();
}


void ServerDialog::handleLoad(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  if (dialog->usedDir.empty())
    dialog->usedDir = core::getuserhomedir();

  Fl_File_Chooser* file_chooser = new Fl_File_Chooser(dialog->usedDir.c_str(),
                                                      _("TigerVNC configuration (*.tigervnc)"),
                                                      0, _("Select a TigerVNC configuration file"));
  file_chooser->preview(0);
  file_chooser->previewButton->hide();
  file_chooser->show();
  
  // Block until user picks something.
  while(file_chooser->shown())
    Fl::wait();
  
  // Did the user hit cancel?
  if (file_chooser->value() == nullptr) {
    delete(file_chooser);
    return;
  }
  
  const char* filename = file_chooser->value();
  dialog->updateUsedDir(filename);

  try {
    dialog->serverName->value(loadViewerParameters(filename));
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    fl_alert(_("Unable to load the specified configuration file:\n\n%s"),
             e.what());
  }

  delete(file_chooser);
}


void ServerDialog::handleSaveAs(Fl_Widget* /*widget*/, void* data)
{ 
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();
  const char* filename;
  if (dialog->usedDir.empty())
    dialog->usedDir = core::getuserhomedir();
  
  Fl_File_Chooser* file_chooser = new Fl_File_Chooser(dialog->usedDir.c_str(),
                                                      _("TigerVNC configuration (*.tigervnc)"),
                                                      2, _("Save the TigerVNC configuration to file"));
  
  file_chooser->preview(0);
  file_chooser->previewButton->hide();
  file_chooser->show();
  
  while(1) {
    
    // Block until user picks something.
    while(file_chooser->shown())
      Fl::wait();
    
    // Did the user hit cancel?
    if (file_chooser->value() == nullptr) {
      delete(file_chooser);
      return;
    }
    
    filename = file_chooser->value();
    dialog->updateUsedDir(filename);
    
    FILE* f = fopen(filename, "r");
    if (f) {

      // The file already exists.
      fclose(f);
      int overwrite_choice = fl_choice(_("%s already exists. Do you want to overwrite?"), 
                                       _("Overwrite"), _("No"), nullptr, filename);
      if (overwrite_choice == 1) {

        // If the user doesn't want to overwrite:
        file_chooser->show();
        continue;
      }
    }

    break;
  }
  
  try {
    saveViewerParameters(filename, servername);
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    fl_alert(_("Unable to save the specified configuration "
               "file:\n\n%s"), e.what());
  }
  
  delete(file_chooser);
}


#ifdef WIN32
void ServerDialog::handleForgetPassword(Fl_Widget*, void* data)
{
  ServerDialog* dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();

  if ((servername == nullptr) || (servername[0] == '\0')) {
    fl_alert(_("Enter a VNC server before forgetting its saved password."));
    return;
  }

  try {
    CConn::forgetSavedCredential(servername);
    fl_message(_("The saved password for \"%s\" was removed."), servername);
  } catch (std::exception& e) {
    vlog.error(_("Unable to remove the saved credential: %s"), e.what());
    fl_alert(_("Unable to forget the saved password:\n\n%s"), e.what());
  }
}
#endif


void ServerDialog::handleAbout(Fl_Widget* /*widget*/, void* /*data*/)
{
  about_vncviewer();
}


void ServerDialog::handleCancel(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  dialog->serverName->value("");
  dialog->hide();
}


void ServerDialog::handleConnect(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();

  dialog->hide();

  try {
    saveViewerParameters(nullptr, servername);
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the default configuration: %s"),
               e.what());
  }

  // avoid duplicates in the history
  dialog->serverHistory.remove(servername);
  dialog->serverHistory.insert(dialog->serverHistory.begin(), servername);

  try {
    dialog->saveServerHistory();
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the server history: %s"), e.what());
  }
}


static bool same_server(const std::string& a, const std::string& b)
{
  std::string hostA, hostB;
  int portA, portB;

#ifndef WIN32
  if ((a.find("/") != std::string::npos) ||
      (b.find("/") != std::string::npos))
    return a == b;
#endif

  try {
    network::getHostAndPort(a.c_str(), &hostA, &portA);
    network::getHostAndPort(b.c_str(), &hostB, &portB);
  } catch (std::exception& e) {
    return false;
  }

  if (hostA != hostB)
    return false;

  if (portA != portB)
    return false;

  return true;
}


void ServerDialog::loadServerHistory()
{
  std::list<std::string> rawHistory;

  serverHistory.clear();

  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    throw std::runtime_error(_("Could not determine VNC state directory path"));

  std::string filepath = core::format("%s/%s", stateDir, SERVER_HISTORY);

  /* Read server history from file */
  FILE* f = fopen(filepath.c_str(), "r");
  if (!f) {
    if (errno == ENOENT) {
#ifdef _WIN32
      // Read the old registry history once for compatibility. A subsequent
      // connection writes it to the AppData state file.
      rawHistory = loadHistoryFromRegKey();
#endif
    } else {
      throw core::posix_error(
        core::format(_("Failed to open \"%s\""), filepath.c_str()), errno);
    }
  } else {
    int lineNr = 0;
    while (!feof(f)) {
      char line[256];

      // Read the next line
      lineNr++;
      if (!fgets(line, sizeof(line), f)) {
        if (feof(f))
          break;

        fclose(f);
        throw core::posix_error(
          core::format(_("Failed to read line %d in file \"%s\""),
                       lineNr, filepath.c_str()),
          errno);
      }

      int len = strlen(line);

      if (len == (sizeof(line) - 1)) {
        fclose(f);
        std::string msg = core::format(_("Failed to read line %d in "
                                         "file \"%s\""),
                                       lineNr, filepath.c_str());
        throw std::runtime_error(
          core::format("%s: %s", msg.c_str(), _("Line too long")));
      }

      if ((len > 0) && (line[len-1] == '\n')) {
        line[len-1] = '\0';
        len--;
      }
      if ((len > 0) && (line[len-1] == '\r')) {
        line[len-1] = '\0';
        len--;
      }

      if (len == 0)
        continue;

      rawHistory.push_back(line);
    }

    if (fclose(f) != 0)
      throw core::posix_error(
        core::format(_("Failed to close \"%s\""), filepath.c_str()),
        errno);
  }

  // Filter out duplicates, even if they have different formats
  for (const std::string& entry : rawHistory) {
    if (std::find_if(serverHistory.begin(), serverHistory.end(),
                     [&entry](const std::string& s) {
                       return same_server(s, entry);
                     }) != serverHistory.end())
      continue;
    serverHistory.push_back(entry);
  }
}

void ServerDialog::saveServerHistory()
{
  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    throw std::runtime_error(_("Could not determine VNC state directory path"));

  if ((core::mkdir_p(stateDir, 0700) == -1) && (errno != EEXIST))
    throw core::posix_error(
      core::format(_("Failed to create directory \"%s\""), stateDir),
      errno);

  std::string filepath = core::format("%s/%s", stateDir, SERVER_HISTORY);
#ifdef WIN32
  std::string temporaryPath = core::format(
    "%s.tmp.%lu", filepath.c_str(), (unsigned long)GetCurrentProcessId());
#else
  std::string temporaryPath = core::format(
    "%s.tmp.%ld", filepath.c_str(), (long)getpid());
#endif

  /* Write server history atomically in the state directory. */
  FILE* f = fopen(temporaryPath.c_str(), "wb");
  if (!f) {
    std::string msg = core::format(_("Failed to open \"%s\""),
                                   temporaryPath.c_str());
    throw core::posix_error(msg.c_str(), errno);
  }

  // Save the last X elements to the config file.
  size_t count = 0;
  for (const std::string& entry : serverHistory) {
    if (++count > SERVER_HISTORY_SIZE)
      break;
    if (fprintf(f, "%s\n", entry.c_str()) < 0) {
      int err = errno ? errno : EIO;
      fclose(f);
      ::remove(temporaryPath.c_str());
      throw core::posix_error(
        core::format(_("Failed to write \"%s\""), temporaryPath.c_str()),
        err);
    }
  }

  if (fflush(f) != 0) {
    int err = errno;
    fclose(f);
    ::remove(temporaryPath.c_str());
    throw core::posix_error(
      core::format(_("Failed to write \"%s\""), temporaryPath.c_str()),
      err);
  }

#ifdef WIN32
  if (_commit(_fileno(f)) != 0) {
#else
  if (fsync(fileno(f)) != 0) {
#endif
    int err = errno;
    fclose(f);
    ::remove(temporaryPath.c_str());
    throw core::posix_error(
      core::format(_("Failed to write \"%s\""), temporaryPath.c_str()),
      err);
  }

  if (fclose(f) != 0) {
    int err = errno;
    ::remove(temporaryPath.c_str());
    throw core::posix_error(
      core::format(_("Failed to close \"%s\""), temporaryPath.c_str()),
      err);
  }

#ifdef WIN32
  if (!MoveFileExA(temporaryPath.c_str(), filepath.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DWORD err = GetLastError();
    ::remove(temporaryPath.c_str());
    throw core::win32_error(
      core::format(_("Failed to replace \"%s\""), filepath.c_str()), err);
  }
#else
  if (rename(temporaryPath.c_str(), filepath.c_str()) != 0) {
    int err = errno;
    ::remove(temporaryPath.c_str());
    throw core::posix_error(
      core::format(_("Failed to replace \"%s\""), filepath.c_str()), err);
  }
#endif
}

void ServerDialog::updateUsedDir(const char* filename)
{
  char * name = strdup(filename);
  usedDir = dirname(name);
  free(name);
}

void ServerDialog::onServerHistoryRemove(Fl_Widget*, std::string s, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  dialog->serverHistory.remove(s);
  try {
    dialog->saveServerHistory();
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the server history: %s"), e.what());
    fl_alert(_("Unable to save the server history:\n\n%s"), e.what());
  }
}

std::string ServerDialog::serverHistoryNormalize(const std::string s)
{
  // Convert to lowercase for case-insensitity
  std::string result = s;
  transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}
