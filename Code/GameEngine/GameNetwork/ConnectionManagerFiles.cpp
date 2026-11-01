/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/ConnectionManager.h"

#include "Common/file.h"
#include "Common/LocalFileSystem.h"
#include "GameNetwork/GameLobbyCallbacks.h"
#include "GameNetwork/networkutil.h"

// ConnectionManager: map transfer between the players before the game.

// Map transfer writes files under a name chosen by the sending player. Only accept the files the
// transfer actually sends: "<Maps|UserData\Maps>\<folder>\<file>" (the portable prefixes of
// GameState) with a known extension -- no drive, no "..", no further subfolders.
static Bool isAllowedTransferFilename(const AsciiString& portable)
{
	const char *prefixes[] = { "UserData\\Maps\\", "Maps\\" };
	const char *rest = nullptr;
	for (const char *prefix : prefixes)
	{
		if (portable.startsWithNoCase(prefix))
		{
			rest = portable.str() + strlen(prefix);
			break;
		}
	}
	if (rest == nullptr || *rest == 0)
		return FALSE;
	if (strstr(rest, "..") || strchr(rest, '/') || strchr(rest, ':'))
		return FALSE;
	const char *slash = strchr(rest, '\\');
	if (slash == nullptr || slash == rest || slash[1] == 0 || strchr(slash + 1, '\\'))
		return FALSE;
	for (const char *c = rest; *c; ++c)
	{
		if (static_cast<unsigned char>(*c) < 32)
			return FALSE;
	}
	const char *dot = strrchr(slash + 1, '.');
	if (dot == nullptr)
		return FALSE;
	const char *extensions[] = { ".map", ".tga", ".ini", ".str", ".txt" };
	for (const char *ext : extensions)
	{
		if (_stricmp(dot, ext) == 0)
			return TRUE;
	}
	return FALSE;
}

// Folder part of a path, up to and without the last separator.
static AsciiString folderOf(const AsciiString& path)
{
	const char *s = path.str();
	const char *last = nullptr;
	for (const char *c = s; *c; ++c)
	{
		if (*c == '\\' || *c == '/')
			last = c;
	}
	AsciiString folder;
	if (last != nullptr)
	{
		for (const char *c = s; c < last; ++c)
			folder.concat(*c);
	}
	return folder;
}

// Map files are only sent by the host (slot 0, see DoAnyMapTransfers) and only belong into the
// folder of this game's map; anything else from a player would overwrite files at will.
static Bool isExpectedTransferSource(NetCommandMsg *msg, const AsciiString& realFilename)
{
	if (msg->getPlayerID() != 0 || TheGameInfo == nullptr)
		return FALSE;
	const AsciiString mapFolder = folderOf(TheGameInfo->getMap());
	return !mapFolder.isEmpty() && mapFolder.compareNoCase(folderOf(realFilename)) == 0;
}

void ConnectionManager::processFile(NetFileCommandMsg *msg) 
{
	if (!isAllowedTransferFilename(msg->getPortableFilename()))
	{
		DEBUG_LOG(("ConnectionManager::processFile() - rejected file name '%s'\n", msg->getPortableFilename().str()));
		return;
	}
	// only a file announced under this command ID with this name (processFileAnnounce)
	FileCommandMap::iterator announced = m_fileCommands.find(msg->getID());
	if (announced == m_fileCommands.end() || announced->second.compareNoCase(msg->getRealFilename()) != 0
		|| !isExpectedTransferSource(msg, msg->getRealFilename()))
	{
		DEBUG_LOG(("ConnectionManager::processFile() - unexpected file '%s' from player %d, dropped\n",
			msg->getPortableFilename().str(), (int)msg->getPlayerID()));
		return;
	}

	if (TheFileSystem->doesFileExist(msg->getRealFilename().str()))
	{
		DEBUG_LOG(("File exists already!\n"));
		//return;
	}

	UnsignedByte *buf = msg->getFileData();
	Int len = msg->getFileLength();

	// uncompress Targas
#ifdef COMPRESS_TARGAS
	Bool deleteBuf = FALSE;
	if (msg->getFilename().endsWith(".tga") && CompressionManager::isDataCompressed(buf, len))
	{
		Int uncompLen = CompressionManager::getUncompressedSize(buf, len);
		UnsignedByte *uncompBuffer = NEW UnsignedByte[uncompLen];
		Int actualLen = CompressionManager::decompressData(buf, len, uncompBuffer, uncompLen);
		if (actualLen == uncompLen)
		{
			DEBUG_LOG(("Uncompressed Targa after map transfer\n"));
			deleteBuf = TRUE;
			buf = uncompBuffer;
			len = uncompLen;
		}
		else
		{
			DEBUG_LOG(("Failed to uncompress Targa after map transfer\n"));
			delete[] uncompBuffer; // failed to decompress, so just use the source
		}
	}
#endif // COMPRESS_TARGAS

	File *fp = TheFileSystem->openFile(msg->getRealFilename().str(), File::CREATE | File::BINARY | File::WRITE);
	if (fp)
	{
		fp->write(buf, len);
		fp->close();
		fp = nullptr;
		DEBUG_LOG(("Wrote %d bytes to file %s!\n",len,msg->getRealFilename().str()));

	}
	else
	{
		DEBUG_LOG(("Cannot open file!\n"));
	}

	DEBUG_LOG(("ConnectionManager::processFile() - sending a NetFileProgressCommandMsg\n"));
	
	Int commandID = msg->getID();
	Int newProgress = 100;

	m_fileProgress[m_localSlot][static_cast<unsigned short>(commandID)] = newProgress;

	Int progressMask = 0xff ^ (1 << m_localSlot);
	NetFileProgressCommandMsg *progressMsg = newInstance(NetFileProgressCommandMsg);
	progressMsg->setPlayerID(m_localSlot);
	progressMsg->setID(0);
	if (DoesCommandRequireACommandID(progressMsg->getNetCommandType()))
	{
		progressMsg->setID(GenerateNextCommandID());
	}
	progressMsg->setFileID(static_cast<UnsignedShort>(commandID));
	progressMsg->setProgress(newProgress);
	sendLocalCommand(progressMsg, static_cast<UnsignedByte>(progressMask));
	processFileProgress(progressMsg);
	progressMsg->detach();

#ifdef COMPRESS_TARGAS
	if (deleteBuf)
	{
		delete[] buf;
		buf = nullptr;
	}
#endif // COMPRESS_TARGAS
}

void ConnectionManager::processFileAnnounce(NetFileAnnounceCommandMsg *msg) 
{
	if (!isAllowedTransferFilename(msg->getPortableFilename()) || !isExpectedTransferSource(msg, msg->getRealFilename()))
	{
		DEBUG_LOG(("ConnectionManager::processFileAnnounce() - rejected file '%s' from player %d\n",
			msg->getPortableFilename().str(), (int)msg->getPlayerID()));
		return;
	}
	DEBUG_LOG(("ConnectionManager::processFileAnnounce() - expecting '%s' (%s) in command %d\n", msg->getPortableFilename().str(), msg->getRealFilename().str(), msg->getFileID()));
	m_fileCommands[msg->getFileID()] = msg->getRealFilename();
	m_fileRecipients[msg->getFileID()] = msg->getPlayerMask();
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if ( (1<<i) & msg->getPlayerMask() )
		{
			m_fileProgress[i][msg->getFileID()] = 0;
		}
		else
		{
			m_fileProgress[i][msg->getFileID()] = 100; // they don't need to get it, so they're already done.
		}
	}
}

void ConnectionManager::processFileProgress(NetFileProgressCommandMsg *msg) 
{
	DEBUG_LOG(("ConnectionManager::processFileProgress() - command %d is at %d%%\n",
		msg->getFileID(), msg->getProgress()));
	Int oldProgress = m_fileProgress[msg->getPlayerID()][msg->getFileID()];

	m_fileProgress[msg->getPlayerID()][msg->getFileID()] = max(oldProgress, msg->getProgress());
}

UnsignedShort ConnectionManager::sendFileAnnounce(AsciiString path, UnsignedByte playerMask)
{
	File *theFile = TheLocalFileSystem->openFile(path.str());
	if (!theFile || !theFile->size())
	{
		UnicodeString log;
		log.format(L"Not sending file '%hs' to %X\n", path.str(), playerMask);
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("%ls\n", log.str()));
		m_events->fileNotSent(log);
		return 0;
	}

	theFile->close();

	Int announceMask = 0xff ^ (1 << m_localSlot);
	NetFileAnnounceCommandMsg *announceMsg = newInstance(NetFileAnnounceCommandMsg);
	announceMsg->setPlayerID(m_localSlot);
	if (DoesCommandRequireACommandID(announceMsg->getNetCommandType()) == TRUE) {
		announceMsg->setID(GenerateNextCommandID());
	}
	announceMsg->setRealFilename(path);
	announceMsg->setPlayerMask(playerMask);
	UnsignedShort fileID = GenerateNextCommandID();
	announceMsg->setFileID(fileID);
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendFileAnnounce() - creating announce message with ID of %d from %d to mask %X for '%s' going to %X as command %d\n",
		announceMsg->getID(), announceMsg->getPlayerID(), announceMask, announceMsg->getRealFilename().str(),
		announceMsg->getPlayerMask(), announceMsg->getFileID()));

	processFileAnnounce(announceMsg); // set up things for the host

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("Sending file announce to %X\n", announceMask));
	sendLocalCommand(announceMsg, static_cast<UnsignedByte>(announceMask));
	announceMsg->detach();

	return fileID;
}

void ConnectionManager::sendFile(AsciiString path, UnsignedByte playerMask, UnsignedShort commandID)
{
	File *theFile = TheLocalFileSystem->openFile(path.str());
	if (!theFile || !theFile->size())
	{
		UnicodeString log;
		log.format(L"Not sending file '%hs' to %X\n", path.str(), playerMask);
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("%ls\n", log.str()));
		m_events->fileNotSent(log);
		return;
	}

	Int len = theFile->size();
	char *buf = theFile->readEntireAndClose();

	// compress Targas
#ifdef COMPRESS_TARGAS
	char *compressedBuf = nullptr;
	Int compressedLen = path.endsWith(".tga")?CompressionManager::getMaxCompressedSize(len, CompressionManager::getPreferredCompression()):0;
	Int compressedSize = 0;
	if (compressedLen)
		compressedSize = CompressionManager::compressData(CompressionManager::getPreferredCompression(),
		buf, len, compressedBuf, compressedLen);

	if (compressedBuf && !compressedSize)
	{
		delete[] compressedBuf;
		compressedBuf = nullptr;
	}
#endif // COMPRESS_TARGAS

	NetFileCommandMsg *fileMsg = newInstance(NetFileCommandMsg);
	fileMsg->setPlayerID(m_localSlot);
	fileMsg->setID(commandID);
	fileMsg->setRealFilename(path);
#ifdef COMPRESS_TARGAS
	if (compressedBuf)
	{
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("Compressed '%s' from %d to %d (%g%%) before transfer\n", path.str(), len, compressedSize,
			(Real)compressedSize/(Real)len*100.0f));
		fileMsg->setFileData((unsigned char *)compressedBuf, compressedSize);
	}
	else
#endif // COMPRESS_TARGAS
	{
		fileMsg->setFileData(reinterpret_cast<unsigned char *>(buf), len);
	}

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::sendFile() - creating file message with ID of %d for '%s' going to %X from %d, size of %d\n",
		fileMsg->getID(), fileMsg->getRealFilename().str(), playerMask, fileMsg->getPlayerID(), fileMsg->getFileLength()));

	delete[] buf;
	buf = nullptr;
#ifdef COMPRESS_TARGAS
	if (compressedBuf)
	{
		delete[] compressedBuf;
		compressedBuf = nullptr;
	}
#endif // COMPRESS_TARGAS

	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("Sending file: '%s', len %d, to %X\n", path.str(), len, playerMask));

	sendLocalCommand(fileMsg, playerMask);

	fileMsg->detach();
}

Int ConnectionManager::getFileTransferProgress(Int playerID, AsciiString path)
{
	FileCommandMap::iterator commandIt = m_fileCommands.begin();
	while (commandIt != m_fileCommands.end())
	{
		//DEBUG_LOG(("ConnectionManager::getFileTransferProgress(%s): looking at existing transfer of '%s'\n",
		//	path.str(), commandIt->second.str()));
		if (commandIt->second == path)
		{
			return m_fileProgress[playerID][commandIt->first];
		}
		++commandIt;
	}
	//DEBUG_LOG(("Falling back to 0, since we couldn't find the map\n"));
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::getFileTransferProgress: path %s not found\n",path.str()));
	return 0;
}
