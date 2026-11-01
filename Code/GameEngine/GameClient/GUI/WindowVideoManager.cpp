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

// FILE: WindowVideoManager.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Apr 2002
//
//	Filename: 	WindowVideoManager.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Every window is setup to be able to draw a movie.  The 
//						WindowVideoManager will take care of setting up the window,
//						creating/destroying the buffers, and the ability to pause/stop
//						movies.
//
//	To Use:		Create a manager and initialize it. Make sure the manager's Update
//						is called every frame or how ever often it needs to be updated.
//						Call reset if the manager needs to be cleared.
//						Play a movie in a window by passing a window pointer, a movie name,
//						and the playtype.
//						If a user trys to play a two different movies on the same window,
//						only the last one added will play.
//						It's important when destroying a window, that window is removed
//						from the manager (Or call Reset if you know no other windows are
//						playing a movie).
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/WindowVideoManager.h"
#include "GameClient/GameWindow.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/Display.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// WindowVideo PUBLIC FUNCTIONS ///////////////////////////////////////////////
//-----------------------------------------------------------------------------
WindowVideo::WindowVideo( void )
{
	
	m_playType = WindowVideoPlayType::WINDOW_PLAY_MOVIE_ONCE;
	m_win = nullptr;
	m_videoBuffer = nullptr;
	m_videoStream = nullptr;
	m_movieName.clear();
	m_state = WindowVideoStates::WINDOW_VIDEO_STATE_STOP;

}

WindowVideo::~WindowVideo( void )
{
	// Don't Delete the window, only set it's video buffer to NULL
	if(m_win)
		m_win->winGetInstanceData()->setVideoBuffer( nullptr );
	m_win = nullptr;
	
	m_videoBuffer.reset();
	m_videoBuffer = nullptr;

	if ( m_videoStream )
		m_videoStream->close();
	m_videoStream = nullptr;

}
	
void WindowVideo::init( GameWindow *win, AsciiString movieName, 
												WindowVideoPlayType playType,
												VideoBuffer *videoBuffer, VideoStreamInterface *videoStream)
{
	m_win = win;
	m_movieName = movieName;
	m_playType = playType;
	m_videoBuffer.reset(videoBuffer);
	m_videoStream = videoStream;
	m_state = WindowVideoStates::WINDOW_VIDEO_STATE_PLAY;
	if(m_win)
		m_win->winGetInstanceData()->setVideoBuffer( m_videoBuffer.get() );
}
	
void WindowVideo::setWindowState( WindowVideoStates state )
{ 
	m_state = state; 

	if(m_state == WindowVideoStates::WINDOW_VIDEO_STATE_STOP && m_win)
		m_win->winGetInstanceData()->setVideoBuffer( nullptr );

	if((m_state == WindowVideoStates::WINDOW_VIDEO_STATE_PLAY || m_state == WindowVideoStates::WINDOW_VIDEO_STATE_PAUSE )&& m_win)
		m_win->winGetInstanceData()->setVideoBuffer( m_videoBuffer.get() );
}	

//-----------------------------------------------------------------------------
// WindowVideoManager PUBLIC FUNCTIONS ////////////////////////////////////////
//-----------------------------------------------------------------------------
WindowVideoManager::WindowVideoManager( void )
{
	m_playingVideos.clear();
	
	m_stopAllMovies = FALSE;
	m_pauseAllMovies = FALSE;

}

WindowVideoManager::~WindowVideoManager( void )
{
	m_playingVideos.clear();
	
}

	
void WindowVideoManager::init( void )
{
	m_playingVideos.clear();
	
	m_stopAllMovies = FALSE;
	m_pauseAllMovies = FALSE;
}

void WindowVideoManager::reset( void )
{
	m_playingVideos.clear();
	
	m_stopAllMovies = FALSE;
	m_pauseAllMovies = FALSE;
}

void WindowVideoManager::update( void )
{
	WindowVideoMap::iterator it = m_playingVideos.begin();

	if(m_pauseAllMovies || m_stopAllMovies)
		return;

	//Iterate through the maps
	while(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		
		if(!winVid)
		{
			DEBUG_CRASH(("There's No WindowVideo in the m_playignVideos list"));
			return;
		}
		GameWindow *win = winVid->getWin();

		if(winVid->getState() == WindowVideoStates::WINDOW_VIDEO_STATE_HIDDEN && (win->winIsHidden() == FALSE))
		{
			resumeMovie(win);
		}

		if(winVid->getState() == WindowVideoStates::WINDOW_VIDEO_STATE_PLAY && win->winIsHidden())
		{
			hideMovie(win);
		}

		// Only advance the frame if we're playing
		if(winVid->getState() != WindowVideoStates::WINDOW_VIDEO_STATE_PLAY)
		{
			it++;
			continue;
		}

		// Get the Stream and the buffer to update for each animation
		VideoStreamInterface *videoStream = winVid->getVideoStream();
		VideoBuffer *videoBuffer = winVid->getVideoBuffer();
		
		if ( videoStream && videoBuffer )
		{
			if ( videoStream->isFrameReady())
			{
				videoStream->frameDecompress();
				videoStream->frameRender( videoBuffer );
				videoStream->frameNext();
				
				// If we reach frame Index of 0, we might have to pause, or loop.
				if ( videoStream->frameIndex() == 0 )
				{
					if(winVid->getPlayType() == WindowVideoPlayType::WINDOW_PLAY_MOVIE_ONCE)
						stopMovie(win);
					else if (winVid->getPlayType() == WindowVideoPlayType::WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME)
						pauseMovie(win);
				}
			}
		}
		
		it++;
	}
}

void WindowVideoManager::playMovie( GameWindow *win, AsciiString movieName, WindowVideoPlayType playType )
{
	// if we already have a movie playing for that window, kill it.
	stopAndRemoveMovie( win );
	
	// create the new stream
	VideoStreamInterface *videoStream = TheVideoPlayer->open( movieName );
	if ( videoStream == nullptr )
	{
		return;
	}

	// Create the new buffer
	VideoBuffer *videoBuffer = TheDisplay->createVideoBuffer();
	if (	videoBuffer == nullptr || 
				!videoBuffer->allocate(	videoStream->width(), 
													videoStream->height())
		)
	{
		// If we failed to create the buffer...
		delete videoBuffer;
		videoBuffer = nullptr;

		if ( videoStream )
			videoStream->close();
		videoStream = nullptr;

		return;
	}

	// now that we have everything, create the new WindowVideo Structure
	auto winVid = std::make_unique<WindowVideo>();
	
	// init it.
	winVid->init( win, movieName,playType,videoBuffer,videoStream);

	// add it to our map.
	m_playingVideos[win] = std::move(winVid);

	m_pauseAllMovies = FALSE;
	m_stopAllMovies = FALSE;
}


void WindowVideoManager::pauseMovie( GameWindow *win )
{
	WindowVideoMap::iterator it = m_playingVideos.find(win);
	if(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		if(winVid)
		winVid->setWindowState(WindowVideoStates::WINDOW_VIDEO_STATE_PAUSE);
	}
	
}
void WindowVideoManager::hideMovie( GameWindow *win )
{
	WindowVideoMap::iterator it = m_playingVideos.find(win);
	if(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		if(winVid)
		winVid->setWindowState(WindowVideoStates::WINDOW_VIDEO_STATE_HIDDEN);
	}
}

void WindowVideoManager::resumeMovie( GameWindow *win )
{
	WindowVideoMap::iterator it = m_playingVideos.find(win);
	if(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		if(winVid)
			winVid->setWindowState(WindowVideoStates::WINDOW_VIDEO_STATE_PLAY);
	}
	m_pauseAllMovies = FALSE;
	m_stopAllMovies = FALSE;
}

void WindowVideoManager::stopMovie( GameWindow *win )
{
	WindowVideoMap::iterator it = m_playingVideos.find(win);
	if(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		if(winVid)
			winVid->setWindowState(WindowVideoStates::WINDOW_VIDEO_STATE_STOP);
	}
}

void WindowVideoManager::stopAndRemoveMovie( GameWindow *win )
{
	WindowVideoMap::iterator it = m_playingVideos.find(win);
	if(it != m_playingVideos.end())
	{
		m_playingVideos.erase(it);
	}
}

void WindowVideoManager::stopAllMovies( void )
{
	WindowVideoMap::iterator it = m_playingVideos.begin();
	//Iterate through the maps
	while(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		if(winVid)
			winVid->setWindowState(WindowVideoStates::WINDOW_VIDEO_STATE_STOP);
		it++;
	}
	
	m_stopAllMovies = TRUE;
	m_pauseAllMovies = FALSE;
}

void WindowVideoManager::pauseAllMovies( void )
{
	WindowVideoMap::iterator it = m_playingVideos.begin();
	//Iterate through the maps
	while(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		if(winVid)
			winVid->setWindowState(WindowVideoStates::WINDOW_VIDEO_STATE_PAUSE);
		it++;
	}
	
	m_pauseAllMovies = TRUE;
	m_stopAllMovies = FALSE;
}

void WindowVideoManager::resumeAllMovies( void )
{
	WindowVideoMap::iterator it = m_playingVideos.begin();
	//Iterate through the maps
	while(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		if(winVid)
			winVid->setWindowState(WindowVideoStates::WINDOW_VIDEO_STATE_PLAY);
		it++;
	}
	m_stopAllMovies = FALSE;
	m_pauseAllMovies = FALSE;
}

Int WindowVideoManager::getWinState( GameWindow *win )
{
	WindowVideoMap::iterator it = m_playingVideos.find(win);
	if(it != m_playingVideos.end())
	{
		WindowVideo *winVid = it->second.get();
		if(winVid)
			return static_cast<Int>(winVid->getState());
	}
	return static_cast<Int>(WindowVideoStates::WINDOW_VIDEO_STATE_STOP);
}

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------