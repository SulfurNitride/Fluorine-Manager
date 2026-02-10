/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NXMURL_H
#define NXMURL_H

#include "dllimport.h"
#include <QList>
#include <QObject>
#include <QString>

/**
 * @brief represents a nxm:// url
 * @todo the game name encoded into the url is not interpreted
 **/
class QDLLEXPORT NXMUrl : public QObject
{
  Q_OBJECT

public:
  /**
   * @brief constructor
   *
   * @param url url following the nxm-protocol
   **/
  NXMUrl(const QString& url);

  /**
   * @return name of the game
   */
  QString game() const { return m_Game; }

  /**
   * @return the NMX request key
   */
  QString key() const { return m_Key; }

  /**
   * @brief retrieve the mod id encoded into the url
   *
   * @return mod id
   **/
  int modId() const { return m_ModId; }

  /**
   * @brief retrieve the file id encoded into the url
   *
   * @return file id
   **/
  int fileId() const { return m_FileId; }

  /**
   * @return the expires timestamp
   */
  int expires() const { return m_Expires; }

  /**
   * @return the parsed user ID
   */
  int userId() const { return m_UserId; }

private:
  QString m_Game;
  QString m_Key;
  int m_ModId;
  int m_FileId;
  int m_Expires;
  int m_UserId;
};

#endif  // NXMURL_H
