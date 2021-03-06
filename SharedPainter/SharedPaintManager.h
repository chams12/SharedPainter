#pragma once

#include <QNetworkInterface>
#include "Singleton.h"
#include "PaintItem.h"
#include "PacketSlicer.h"
#include "PaintPacketBuilder.h"
#include "WindowPacketBuilder.h"
#include "BroadCastPacketBuilder.h"
#include "SystemPacketBuilder.h"
#include "SharedPaintPolicy.h"
#include "DefferedCaller.h"
#include "SharedPaintManagementData.h"
#include "SharedPaintCommandManager.h"
#include "PaintSession.h"
#include "NetPeerServer.h"
#include "NetBroadCastSession.h"
#include "NetServiceRunner.h"
#include "PaintUser.h"

#define SharePaintManagerPtr()		CSingleton<CSharedPaintManager>::Instance()

class CSharedPaintManager;

class ISharedPaintEvent
{
public:
	virtual void onISharedPaintEvent_Connected( CSharedPaintManager *self ) = 0;
	virtual void onISharedPaintEvent_ConnectFailed( CSharedPaintManager *self ) = 0;
	virtual void onISharedPaintEvent_SendingPacket( CSharedPaintManager *self, int packetId, size_t wroteBytes, size_t totalBytes ) = 0;
	virtual void onISharedPaintEvent_Disconnected( CSharedPaintManager *self ) = 0;
	virtual void onISharedPaintEvent_AddPaintItem( CSharedPaintManager *self, boost::shared_ptr<CPaintItem> item ) = 0;
	virtual void onISharedPaintEvent_UpdatePaintItem( CSharedPaintManager *self, boost::shared_ptr<CPaintItem> item ) = 0;
	virtual void onISharedPaintEvent_RemovePaintItem( CSharedPaintManager *self, const std::string &owner, int itemId ) = 0;
	virtual void onISharedPaintEvent_MovePaintItem( CSharedPaintManager *self, const std::string &owner, int itemId, double x, double y ) = 0;
	virtual void onISharedPaintEvent_ResizeMainWindow( CSharedPaintManager *self, int width, int height ) = 0;
	virtual void onISharedPaintEvent_GetServerInfo( CSharedPaintManager *self, const std::string &broadcastChannel, const std::string &addr, int port ) = 0;
	virtual void onISharedPaintEvent_SetBackgroundImage( CSharedPaintManager *self, boost::shared_ptr<CBackgroundImageItem> image ) = 0;
	virtual void onISharedPaintEvent_ClearScreen( CSharedPaintManager *self ) = 0;
	virtual void onISharedPaintEvent_ClearBackgroundImage( CSharedPaintManager *self ) = 0;
	virtual void onISharedPaintEvent_UpdatePaintUser( CSharedPaintManager *self, boost::shared_ptr<CPaintUser> user ) = 0;
};


class CSharedPaintManager : public INetPeerServerEvent, INetBroadCastSessionEvent, IPaintSessionEvent
{
private:
	typedef std::map< std::string, boost::shared_ptr<CSharedPaintItemList> > ITEM_LIST_MAP;
	typedef std::map< std::string, boost::shared_ptr<CPaintUser> > USER_MAP;
	typedef std::vector< boost::shared_ptr<CPaintSession> > SESSION_LIST;

public:
	CSharedPaintManager(void);
	~CSharedPaintManager(void);

	const std::string& myId( void ) 
	{
		return myId_;
	}

	void close( void )
	{
		if( netPeerServer_ )
			netPeerServer_->close();

		if( broadCastSession_ )
			broadCastSession_->close();

		clearAllUsers();
		clearAllSessions();

		netRunner_.close();
	}

	void setCanvas( IGluePaintCanvas *canvas )
	{
		canvas_ = canvas;
	}

	void registerObserver( ISharedPaintEvent *obs )
	{
		observers_.remove( obs );
		observers_.push_back( obs );
	}

	void unregisterObserver( ISharedPaintEvent *obs )
	{
		observers_.remove( obs );
	}

	// Network
public:
	bool startClient( void );
	void startServer( const std::string &broadCastChannel, int port = 0 );
	void setBroadCastChannel( const std::string & channel );

	bool connectToPeer( const std::string &addr, int port )
	{
		clearAllItems();
		clearAllUsers();

		boost::shared_ptr<CNetPeerSession> session = netRunner_.newSession();
		boost::shared_ptr<CPaintSession> userSession = boost::shared_ptr<CPaintSession>(new CPaintSession(session, this));

		mutexSession_.lock();
		sessionList_.push_back( userSession );
		mutexSession_.unlock();

		// must be called here for preventing from a crash by thread race condition.
		userSession->session()->connect( addr, port );

		return true;
	}

	int acceptPort( void ) const
	{
		return acceptPort_;
	}

	int lastPacketId( void )
	{
		return lastPacketId_;
	}

	bool isServerMode( void )
	{
		return serverMode_;
	}

	bool isConnecting( void )
	{
		boost::recursive_mutex::scoped_lock autolock(mutexSession_);

		SESSION_LIST::iterator it = sessionList_.begin();
		for( ; it != sessionList_.end(); it++ )
		{
			if( (*it)->session()->isConnecting() )
				return true;
		}
		return false;
	}

	bool isConnected( void )
	{
		boost::recursive_mutex::scoped_lock autolock(mutexSession_);

		SESSION_LIST::iterator it = sessionList_.begin();
		for( ; it != sessionList_.end(); it++ )
		{
			if( (*it)->session()->isConnected() )
				return true;
		}
		return false;
	}

	int sendDataToUsers( const std::vector<boost::shared_ptr<CPaintSession>> &sessionList, const std::string &msg, int toSessionId = -1 )
	{
		static int PACKETID = 0;
		int sendCnt = 0;
		int packetId = ++PACKETID;
		std::vector<struct send_byte_info_t> infolist;

		SESSION_LIST::const_iterator it = sessionList.begin();
		for( ; it != sessionList.end(); it++ )
		{
			if( (*it)->session()->isConnected() )
			{
				if( toSessionId >= 0 && (*it)->sessionId() != toSessionId )
					continue;

				struct send_byte_info_t info;
				info.session = (*it).get();
				info.totalBytes = msg.size();
				info.wroteBytes = 0;

				infolist.push_back( info );
				boost::shared_ptr<CNetPacketData> packet = boost::shared_ptr<CNetPacketData>(new CNetPacketData( packetId, msg ) );
				(*it)->session()->sendData( packet );
				sendCnt ++;
			}
		}

		if ( sendCnt > 0 )
		{
			mutexSendInfo_.lock();
			sendInfoDataMap_.insert( send_info_map_t::value_type( packetId, infolist ) );
			lastPacketId_ = packetId;
			mutexSendInfo_.unlock();
		}
		else
			packetId = -1;

		return packetId;
	}

	int sendDataToUsers( const std::string &msg, int toSessionId = -1 )
	{
		std::vector<boost::shared_ptr<CPaintSession> > sessionList = sessionList_;

		return sendDataToUsers( sessionList, msg, toSessionId );
	}

	// Shared Paint Action
public:
	void undoCommand( void )
	{
		commandMngr_.undoCommand();
	}

	void sendAllSyncData( int toSessionId )
	{
		if( isServerMode() == false )
			return;

		std::string allData;

		// User Info
		allData += generateJoinerInfoPacket();

		// Window Resize
		std::string msg = WindowPacketBuilder::CResizeMainWindow::make( lastWindowWidth_, lastWindowHeight_ );
		allData += msg;

		// Back Ground Image
		if( backgroundImageItem_ )
			allData += PaintPacketBuilder::CSetBackgroundImage::make( backgroundImageItem_ );
		
		// All Paint Item
		ITEM_LIST_MAP::iterator it = userItemListMap_.begin();
		for( ; it != userItemListMap_.end(); it++ )
		{
			CSharedPaintItemList::ITEM_MAP &map = it->second->itemMap();
			CSharedPaintItemList::ITEM_MAP::iterator itItem = map.begin();
			for( ; itItem != map.end(); itItem++ )
			{
				std::string msg = PaintPacketBuilder::CAddItem::make( itItem->second );
				allData += msg;
			}
		}

		sendDataToUsers( allData, toSessionId );
		
	}

	bool sendPaintItem( boost::shared_ptr<CPaintItem> item )
	{
		boost::shared_ptr<CAddItemCommand> command = boost::shared_ptr<CAddItemCommand>(new CAddItemCommand( this, item ));
		return commandMngr_.executeCommand( command );
	}

	int sendBackgroundImage( boost::shared_ptr<CBackgroundImageItem> image )
	{
		if( !image )
			return -1;

		backgroundImageItem_ = image;

		canvas_->drawBackgroundImage( image );

		if( isConnected() == false )
			return -1;

		std::string msg = PaintPacketBuilder::CSetBackgroundImage::make( image );
		return sendDataToUsers( msg );
	}

	void clearBackgroundImage( void )
	{
		backgroundImageItem_ = boost::shared_ptr<CBackgroundImageItem>();
		canvas_->clearBackgroundImage();

		std::string msg = PaintPacketBuilder::CClearBackgroundImage::make();
		sendDataToUsers( msg );
	}

	void clearScreen( void )
	{
		clearAllItems();

		std::string msg = PaintPacketBuilder::CClearScreen::make();
		sendDataToUsers( msg );
	}
	
	void notifyUpdateItem( boost::shared_ptr< CPaintItem > item )
	{
		boost::shared_ptr<CUpdateItemCommand> command = boost::shared_ptr<CUpdateItemCommand>(new CUpdateItemCommand( this, item ));
		commandMngr_.executeCommand( command );
	}

	void notifyMoveItem( boost::shared_ptr< CPaintItem > item )
	{
		boost::shared_ptr<CMoveItemCommand> command = boost::shared_ptr<CMoveItemCommand>(new CMoveItemCommand( this, item ));
		commandMngr_.executeCommand( command );
	}

	void notifyRemoveItem( boost::shared_ptr< CPaintItem > item )
	{
		boost::shared_ptr<CRemoveItemCommand> command = boost::shared_ptr<CRemoveItemCommand>(new CRemoveItemCommand( this, item ));
		commandMngr_.executeCommand( command );
	}

	int notifyResizingMainWindow( int width, int height )
	{
		std::string msg = WindowPacketBuilder::CResizeMainWindow::make( width, height );
		lastWindowWidth_ = width;
		lastWindowHeight_ = height;
		return sendDataToUsers( msg );
	}

	// User Management and Sync
public:
	int userCount( void )
	{
		return joinerMap_.size();
	}

private:
	void sendMyUserInfo( boost::shared_ptr<CPaintSession> session )
	{
		std::string msg = SystemPacketBuilder::CJoinerUser::make( myUserInfo_ );
		session->session()->sendData( msg );
	}

	void notifyRemoveUserInfo( boost::shared_ptr<CPaintUser> user )
	{
		std::string msg = SystemPacketBuilder::CLeftUser::make( user->userId() );
		sendDataToUsers( msg );
	}

	void addUser( boost::shared_ptr<CPaintUser> user )
	{
		mutexUser_.lock();
		std::pair<USER_MAP::iterator, bool> res = joinerMap_.insert( USER_MAP::value_type( user->userId(), user ) );
		if( !res.second )
			res.first->second = user;	// overwrite;
		mutexUser_.unlock();

		caller_.performMainThread( boost::bind( &CSharedPaintManager::fireObserver_UpdatePaintUser, this, user ) );
	}

	boost::shared_ptr<CPaintUser> findUser( int sessionId )
	{
		boost::recursive_mutex::scoped_lock autolock(mutexUser_);

		USER_MAP::iterator it = joinerMap_.begin();
		for( ; it != joinerMap_.end(); it++ )
		{
			if( it->second->sessionId() == sessionId )
			{
				return it->second;
			}
		}
		return boost::shared_ptr<CPaintUser>();
	}

	boost::shared_ptr<CPaintUser> findUser( const std::string &userId )
	{
		boost::recursive_mutex::scoped_lock autolock(mutexUser_);

		USER_MAP::iterator it = joinerMap_.find( userId );
		if( it != joinerMap_.end() )
			return it->second;
		return boost::shared_ptr<CPaintUser>();
	}

	void removeUser( const std::string & userId )
	{
		boost::shared_ptr<CPaintUser> removing;
		// erase from map
		{
			boost::recursive_mutex::scoped_lock autolock(mutexUser_);

			USER_MAP::iterator it = joinerMap_.find( userId );
			if( it == joinerMap_.end() )
				return;

			removing = it->second;
			joinerMap_.erase( it );
		}

		if( removing )
			caller_.performMainThread( boost::bind( &CSharedPaintManager::fireObserver_UpdatePaintUser, this, removing ) );
	}

	void removeUser( boost::shared_ptr<CPaintUser> user )
	{
		removeUser( user->userId() ); 
	}

	void removeUser( int sessionId )
	{
		boost::shared_ptr<CPaintUser> removing = findUser( sessionId );

		removeUser( removing );
	}

	void clearAllSessions( void )
	{
		mutexSession_.lock();
		SESSION_LIST::iterator it = sessionList_.begin();
		for( ; it != sessionList_.end(); it++ )
		{
			(*it)->close();
		}
		mutexSession_.unlock();
	}

	void clearAllUsers( void )
	{
		boost::recursive_mutex::scoped_lock autolock(mutexUser_);
		joinerMap_.clear();
		joinerMap_.insert( USER_MAP::value_type(myUserInfo_->userId(), myUserInfo_) );
	}

	std::string generateJoinerInfoPacket( void )
	{
		std::string allData;

		// User Info
		mutexUser_.lock();
		USER_MAP::iterator it = joinerMap_.begin();
		for( ; it != joinerMap_.end(); it++ )
		{
			std::string msg = SystemPacketBuilder::CJoinerUser::make( it->second );
			allData += msg;
		}
		mutexUser_.unlock();

		return allData;
	}

	// Internal Action ( for SharedPaintCommand )
public:
	void addPaintItem( boost::shared_ptr<CPaintItem> item )
	{
		assert( item->itemId() > 0 );
		assert( item->owner().empty() == false );

		boost::shared_ptr<CSharedPaintItemList> itemList;
		ITEM_LIST_MAP::iterator it = userItemListMap_.find( item->owner() );
		if( it != userItemListMap_.end() )
		{
			itemList = it->second;
		}
		else
		{
			itemList = boost::shared_ptr<CSharedPaintItemList>( new CSharedPaintItemList( item->owner() ) );
			userItemListMap_.insert( ITEM_LIST_MAP::value_type(item->owner(), itemList) );
		}

		itemList->addItem( item );

		if( !caller_.isMainThread() )
			caller_.performMainThread( boost::bind( &CSharedPaintManager::fireObserver_AddPaintItem, this, item ) );
		else
			fireObserver_AddPaintItem( item );
	}
	
	void updatePaintItem( boost::shared_ptr<CPaintItem> item )
	{
		assert( item->itemId() > 0 );
		assert( item->owner().empty() == false );
	
		if( !caller_.isMainThread() )
			caller_.performMainThread( boost::bind( &CSharedPaintManager::fireObserver_UpdatePaintItem, this, item ) );
		else
			fireObserver_UpdatePaintItem( item );
	}

	void removePaintItem( const std::string &owner, int itemId )
	{
		if( itemId < 0 )
			return;

		boost::shared_ptr<CSharedPaintItemList> itemList = findItemList( owner );
		if( !itemList )
			return;

		boost::shared_ptr<CPaintItem> item = itemList->findItem( itemId );
		if( !item )
			return;

		item->remove();
	
		itemList->removeItem( itemId );
	}

	boost::shared_ptr<CPaintItem> findItem( const std::string &owner, int itemId )
	{
		boost::shared_ptr<CSharedPaintItemList> itemList = findItemList( owner );
		if( !itemList )
			return boost::shared_ptr<CPaintItem>();

		return itemList->findItem( itemId );
	}

	ITEM_LIST findItem( int packetId )	// always find in my item list
	{
		ITEM_LIST resList;
		boost::shared_ptr<CSharedPaintItemList> itemList = findItemList( myId() );
		if( !itemList )
			return resList;

		CSharedPaintItemList::ITEM_MAP::iterator itItem = itemList->itemMap().begin();
		for( ; itItem != itemList->itemMap().end(); itItem++ )
		{
			if( itItem->second->packetId() == packetId )
			{
				resList.push_back( itItem->second );
			}
		}

		return resList;
	}

	void clearAllItems( void )
	{
		backgroundImageItem_ = boost::shared_ptr<CBackgroundImageItem>();
		canvas_->clearScreen();

		// all data clear
		userItemListMap_.clear();
		commandMngr_.clear();
	}

private:
	void dispatchBroadCastPacket( boost::shared_ptr<CPacketData> packetData );
	void dispatchPaintPacket( boost::shared_ptr<CPaintSession> session, boost::shared_ptr<CPacketData> packetData );

	boost::shared_ptr<CSharedPaintItemList> findItemList( const std::string &owner )
	{
		ITEM_LIST_MAP::iterator it = userItemListMap_.find( owner );
		if( it == userItemListMap_.end() )
			return boost::shared_ptr<CSharedPaintItemList>();

		return it->second;
	}

	boost::shared_ptr<CPaintSession> findSession( int sessionId )
	{
		boost::recursive_mutex::scoped_lock autolock(mutexSession_);

		SESSION_LIST::iterator it = sessionList_.begin();
		for( ; it != sessionList_.end(); it++ )
		{
			if( (*it)->sessionId() == sessionId )
			{
				return *it;
			}
		}
		return boost::shared_ptr<CPaintSession>();
	}


private:
	// observer methods
	void fireObserver_ConnectFailed( void )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_ConnectFailed( this );
		}
	}
	void fireObserver_Connected( int sessionId )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_Connected( this );
		}

		if( isServerMode() )
			sendAllSyncData( sessionId );
	}
	void fireObserver_DisConnected( void )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_Disconnected( this );
		}
	}

	void fireObserver_UpdatePaintItem( boost::shared_ptr<CPaintItem> item )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_UpdatePaintItem( this, item );
		}
	}
	
	void fireObserver_AddPaintItem( boost::shared_ptr<CPaintItem> item )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_AddPaintItem( this, item );
		}
	}
	void fireObserver_MovePaintItem( const std::string &owner, int itemId, double x, double y )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_MovePaintItem( this, owner, itemId, x, y );
		}
	}
	void fireObserver_RemovePaintItem( const std::string &owner, int itemId )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_RemovePaintItem( this, owner, itemId );
		}
	}
	void fireObserver_ResizeMainWindow( int width, int height )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_ResizeMainWindow( this, width, height );
		}
	}
	void fireObserver_SendingPacket( int packetId, size_t wroteBytes, size_t totalBytes )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_SendingPacket( this, packetId, wroteBytes, totalBytes );
		}
	}
	void fireObserver_ClearScreen( void )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_ClearScreen( this );
		}
	}
	void fireObserver_ClearBackgroundImage( void )
	{
		backgroundImageItem_ = boost::shared_ptr<CBackgroundImageItem>(); // clear

		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_ClearBackgroundImage( this );
		}
	}
	void fireObserver_SetBackgroundImage( boost::shared_ptr<CBackgroundImageItem> image )
	{
		backgroundImageItem_ = image;

		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_SetBackgroundImage( this, image );
		}
	}
	void fireObserver_GetServerInfo( const std::string &broadcastChannel, const std::string &addr, int port )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_GetServerInfo( this, broadcastChannel, addr, port );
		}
	}
	void fireObserver_UpdatePaintUser( boost::shared_ptr<CPaintUser> user )
	{
		std::list<ISharedPaintEvent *> observers = observers_;
		for( std::list<ISharedPaintEvent *>::iterator it = observers.begin(); it != observers.end(); it++ )
		{
			(*it)->onISharedPaintEvent_UpdatePaintUser( this, user );
		}
	}

	// delaying remove session feature
private:
	void _delayedRemoveSession( int sessionId )
	{
		boost::recursive_mutex::scoped_lock autolock(mutexSession_);

		SESSION_LIST::iterator it = sessionList_.begin();
		for( ; it != sessionList_.end(); it++ )
		{
			if( (*it)->session()->sessionId() == sessionId )
			{
				sessionList_.erase(it);
				break;
			}
		}
	}
	void removeSession( int sessionId )
	{
		caller_.performMainThread( boost::bind(&CSharedPaintManager::_delayedRemoveSession, this, sessionId) );
	}

protected:
	void commonSessionConnection( boost::shared_ptr<CPaintSession> userSession )
	{
		// send to my user info
		sendMyUserInfo( userSession );
	}

	// INetPeerServerEvent
	virtual void onINetPeerServerEvent_Accepted( boost::shared_ptr<CNetPeerServer> server, boost::shared_ptr<CNetPeerSession> session )
	{
		boost::shared_ptr<CPaintSession> userSession = boost::shared_ptr<CPaintSession>(new CPaintSession(session, this));
		
		mutexSession_.lock();
		sessionList_.push_back( userSession );
		mutexSession_.unlock();

		// start read io!
		session->start();
	}

	// INetBroadCastSessionEvent
	virtual void onINetBroadCastSessionEvent_BroadCastReceived( CNetBroadCastSession *session, const std::string buffer )
	{
		broadCastPacketSlicer_.addBuffer( buffer );

		if( broadCastPacketSlicer_.parse() == false )
			return;

		for( size_t i = 0; i < broadCastPacketSlicer_.parsedItemCount(); i++ )
		{
			boost::shared_ptr<CPacketData> data = broadCastPacketSlicer_.parsedItem( i );

			dispatchBroadCastPacket( data );
		}
	}

	// IPaintSessionEvent
	virtual void onIPaintSessionEvent_Connected( boost::shared_ptr<CPaintSession> session )
	{
		commonSessionConnection( session );

		caller_.performMainThread( boost::bind( &CSharedPaintManager::fireObserver_Connected, this, session->sessionId() ) );
	}

	virtual void onIPaintSessionEvent_ConnectFailed( boost::shared_ptr<CPaintSession> session )
	{
		caller_.performMainThread( boost::bind( &CSharedPaintManager::fireObserver_ConnectFailed, this ) );

		removeSession( session->sessionId() );
	}

	virtual void onIPaintSessionEvent_ReceivedPacket( boost::shared_ptr<CPaintSession> session, const boost::shared_ptr<CPacketData> data )
	{
		dispatchPaintPacket( session, data );

		if( isServerMode() )
		{
			// to send the others without this user
			if( sessionList_.size() <= 1 )
				return;

			SESSION_LIST list = sessionList_;
			SESSION_LIST::iterator it = list.begin();
			for( ; it != list.end(); it++ )
			{
				if( *it == session )
				{
					list.erase( it );
					break;
				}
			}

			std::string msg = CommonPacketBuilder::makePacket( data->code, data->body );

			sendDataToUsers( list, msg );
		}
	}

	virtual void onIPaintSessionEvent_Disconnected( boost::shared_ptr<CPaintSession> session )
	{
		if( isConnected() == false )
			caller_.performMainThread( boost::bind( &CSharedPaintManager::fireObserver_DisConnected, this ) );

		boost::shared_ptr<CPaintUser> user = findUser( session->sessionId() );
		if( user )
		{
			if( isServerMode() )
			{
				notifyRemoveUserInfo( user );
			}
			removeUser( user );
		}

		removeSession( session->sessionId() );
	}

	virtual void onIPaintSessionEvent_SendingPacket( boost::shared_ptr<CPaintSession> session, const boost::shared_ptr<CNetPacketData> packet )
	{
		//qDebug() << "Packet sending " << packet->packetId() << packet->buffer().remainingSize() << packet->buffer().totalSize();
		if( packet->packetId() < 0 )
			return;	// ignore this!

		size_t totalBytes = 0;
		size_t wroteBytes = 0;
	
		// processing wrote bytes for all joiners
		{
			boost::recursive_mutex::scoped_lock autolock(mutexSendInfo_);

			send_info_map_t::iterator it = sendInfoDataMap_.find( packet->packetId() );
			if( it == sendInfoDataMap_.end() )
				return;	// not found. unexpected error..

			std::vector<struct send_byte_info_t>::iterator itD = it->second.begin();
			for( ; itD != it->second.end(); itD++ )
			{
				if( (*itD).session == session.get() )
				{
					(*itD).wroteBytes = packet->buffer().totalSize() -  packet->buffer().remainingSize();
					break;
				}
			}

			for( size_t i = 0; i < it->second.size(); i++ )
			{
				const struct send_byte_info_t &info = it->second[i];
				totalBytes += info.totalBytes;
				wroteBytes += info.wroteBytes;
			}

			if( totalBytes <= wroteBytes )
			{
				//qDebug() << "sendInfoDataMap_.erase!!i!!" << packet->packetId() << wroteBytes << totalBytes;
				sendInfoDataMap_.erase( it );
			}
		}

		caller_.performMainThread( boost::bind( &CSharedPaintManager::fireObserver_SendingPacket, this, packet->packetId(), wroteBytes, totalBytes ) );
	}

private:
	std::string myId_;
	CDefferedCaller caller_;

	// obsevers
	std::list<ISharedPaintEvent *> observers_;

	// my action command
	CSharedPaintCommandManager commandMngr_;

	// paint item
	IGluePaintCanvas *canvas_;
	ITEM_LIST_MAP userItemListMap_;
	boost::shared_ptr<CBackgroundImageItem> backgroundImageItem_;
	int lastWindowWidth_;
	int lastWindowHeight_;

	// user management
	boost::recursive_mutex mutexUser_;
	boost::shared_ptr<CPaintUser> myUserInfo_;
	USER_MAP joinerMap_;
	
	// network
	CNetServiceRunner netRunner_;
	bool serverMode_;
	int acceptPort_;
	SESSION_LIST sessionList_;
	boost::recursive_mutex mutexSession_;
	boost::shared_ptr<CNetPeerServer> netPeerServer_;
	CPacketSlicer broadCastPacketSlicer_;
	boost::shared_ptr< CNetBroadCastSession > broadCastSession_;

	// seding byte management
	boost::recursive_mutex mutexSendInfo_;
	struct send_byte_info_t
	{
		int wroteBytes;
		int totalBytes;
		CPaintSession *session;
	};
	typedef std::map< int, std::vector<struct send_byte_info_t> > send_info_map_t;
	send_info_map_t sendInfoDataMap_;
	int lastPacketId_;
};
