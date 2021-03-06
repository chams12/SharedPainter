#pragma once

struct SPaintUserInfoData
{
	std::string userId;
};

class CPaintUser
{
public:
	CPaintUser( void ) { }
	~CPaintUser( void ) { }

	void setSessionId( int sessionId ) { sessionId_ = sessionId; }
	int sessionId( void ) { return sessionId_; }
	void loadData( const struct SPaintUserInfoData &info ) { data_ = info; }

	const std::string &userId( void ) { return data_.userId; }

private:
	int sessionId_;
	SPaintUserInfoData data_;
};