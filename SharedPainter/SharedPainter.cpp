#include "StdAfx.h"
#include "sharedpainter.h"
#include "TextItemDialog.h"

static const int DEFAULT_HIDE_POS_X = 9999;
static const int DEFAULT_HIDE_POS_Y = 9999;

SharedPainter::SharedPainter(CSharedPainterScene *canvas, QWidget *parent, Qt::WFlags flags)
	: QMainWindow(parent, flags), canvas_(canvas), currPaintItemId_(1), currPacketId_(-1), resizeFreezingFlag_(false), screenShotMode_(false), wroteProgressBar_(NULL)
{
	ui.setupUi(this);

	ui.painterView->setScene( canvas );
	canvas_->setEvent( this );

	SharePaintManagerPtr()->registerObserver( this );
	SharePaintManagerPtr()->setCanvas( canvas_ );
	
	QMenuBar *menuBar = ui.menuBar;

	// Create Menu bar item
	{
		// File Menu
		QMenu* file = new QMenu( "&File", menuBar );
		file->addAction( "&Connect", this, SLOT(actionConnect()), Qt::CTRL+Qt::Key_N );
		file->addAction( "&Broadcast Channel", this, SLOT(actionBroadcastChannel()), Qt::CTRL+Qt::Key_H );
		QMenu* broadCastTypeMenu = file->addMenu( "BroadCast Type" );
		broadCastTypeMenu->addAction( "&Server", this, SLOT(actionServerType()), Qt::CTRL+Qt::Key_1 );
		broadCastTypeMenu->addAction( "&Client", this, SLOT(actionClientType()), Qt::CTRL+Qt::Key_2 );
		file->addSeparator();
		file->addAction( "E&xit", this, SLOT(actionExit()), Qt::CTRL+Qt::Key_Q );
		menuBar->addMenu( file );

		// Edit Menu
		QMenu* edit = new QMenu( "&Edit", menuBar );
		QMenu* penMenu = edit->addMenu( "Pen Setting" );
		penMenu->addAction( "Pen &Width", this, SLOT(actionPenWidth()), Qt::CTRL+Qt::Key_V );
		penMenu->addAction( "Pen &Color", this, SLOT(actionPenColor()), Qt::CTRL+Qt::Key_C );
		penModeAction_ = edit->addAction( "Pen Mode", this, SLOT(actionPenMode()), Qt::CTRL+Qt::Key_A );
		penModeAction_->setCheckable( true );
		edit->addAction( "&Text", this, SLOT(actionAddText()), Qt::Key_Enter|Qt::Key_Return );
		edit->addAction( "&Screen Shot", this, SLOT(actionScreenShot()), Qt::CTRL+Qt::Key_S );
		edit->addSeparator();
		edit->addAction( "Clear &Background Image", this, SLOT(actionClearBGImage()), Qt::CTRL+Qt::Key_B );
		edit->addAction( "Cl&ear Screen", this, SLOT(actionClearScreen()), Qt::CTRL+Qt::Key_X );
		edit->addSeparator();
		edit->addAction( "&Undo", this, SLOT(actionUndo()), Qt::CTRL+Qt::Key_Z );
		menuBar->addMenu( edit );
	}

	// create status bar
	{
		statusBarLabel_ = new QLabel();
		broadCastTypeLabel_ = new QLabel();
		joinerCountLabel_ = new QLabel();
		wroteProgressBar_ = new QProgressBar();
		ui.statusBar->addPermanentWidget( broadCastTypeLabel_ );
		ui.statusBar->addPermanentWidget( joinerCountLabel_, 1 );
		ui.statusBar->addPermanentWidget( wroteProgressBar_ );
		ui.statusBar->addPermanentWidget( statusBarLabel_ );

		setStatusBar_Network( tr("Not Connected") );
		setStatusBar_BroadCastType( tr("None Type") );
		setStatusBar_JoinerCnt( 1 );	// my self 
	}
	
	ui.painterView->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	ui.painterView->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	setCursor( Qt::ArrowCursor ); 

	// Pen mode activated..
	penModeAction_->setChecked( true );
	actionPenMode();

	// Key Hooking Timer 
	keyHookTimer_ = new QTimer(this);
	keyHookTimer_->start(20);
	connect(keyHookTimer_, SIGNAL(timeout()),this, SLOT(onTimer()));

	installEventFilter(this);

	// Title
	QString orgTitle = windowTitle();
	QString newTitle = orgTitle;
	newTitle += " Ver ";
	newTitle += VERSION_TEXT;
	newTitle += ", ";
	newTitle += AUTHOR_TEXT;

	setWindowTitle( newTitle );
}

SharedPainter::~SharedPainter()
{
	SharePaintManagerPtr()->unregisterObserver( this );
	SharePaintManagerPtr()->close();

	delete keyHookTimer_;
}

/*
static bool checkKeyPressed( int virtKey )
{
	bool res = false;
#ifdef Q_WS_WIN
	static WORD pressKey[256] = {0, };

	WORD state = ::GetAsyncKeyState( virtKey );
	if( state & 0x1 )
	{
		if( pressKey[ virtKey ] == 0 )
		{
			res = true;
		}
	}
	pressKey[ virtKey ] = state;
#endif
	return res;
}
*/

void SharedPainter::onTimer( void )
{
	//if(!isActiveWindow())
	//	return;
	//
	//if( checkKeyPressed( 'P' ) )
	//{
	//	penModeAction_->setChecked( !penModeAction_->isChecked() );
	//	actionPenMode();	
	//}
	//else if( checkKeyPressed( 'T' ) )
	//{
	//	actionAddText();
	//}
}


// this logic is for fixing bug that the center "Enter key" don't work.
bool SharedPainter::eventFilter(QObject *object, QEvent *event)
{
	if( event->type() == QEvent::KeyPress )
	{
		QKeyEvent *keyEvt = (QKeyEvent*)event;
		if( keyEvt->key() == 0x1000004 )
		{
			actionAddText();
		}
	}
	return QMainWindow::eventFilter(object,event);
}


void SharedPainter::actionExit( void )
{
	close();
}

void SharedPainter::actionConnect( void )
{
	QString errorMsg;

	do{
		bool ok = false;

		static QString lastAddress;

		QString addr = QInputDialog::getText( this, tr("Input peer address"),
			tr("Address:Port"), QLineEdit::Normal, SettingManagerPtr()->peerAddress().c_str(), &ok);

		lastAddress = addr;

		if ( !ok )
			break;

		if ( addr.isEmpty())
		{
			errorMsg = tr("Your input addres is wrong format. (IP:PORT)");
			break;
		}

		QStringList list = addr.split(":");

		if( list.size() != 2 )
		{
			errorMsg = tr("Your input addres is wrong format. (IP:PORT)");
			break;
		}

		std::string ip = list.at(0).toStdString();
		int port = list.at(1).toInt();
		//ip = "61.247.198.102";
		//ip = "127.0.0.1";
		SharePaintManagerPtr()->connectToPeer( ip, port );

		SettingManagerPtr()->setPeerAddress( addr.toStdString() );

		return;
	} while( false );

	if( ! errorMsg.isEmpty() )
		QMessageBox::warning(this, "", errorMsg);
}


QPointF SharedPainter::_calculateTextPos( int textSize )
{
	qDebug() << QCursor::pos() << ui.painterView->mapFromGlobal(QCursor::pos());

	static double lastX = 0;
	static double lastY = 0;
	static double lastMX = 0;
	static double lastMY = 0;

	int sW = canvas_->sceneRect().width();
	int sH = canvas_->sceneRect().height();
	int w = DEFAULT_TEXT_ITEM_POS_REGION_W; if( w > sW ) w = sW;
	int h = DEFAULT_TEXT_ITEM_POS_REGION_H; if( h > sH ) h = sH;

	double mX = ui.painterView->mapFromGlobal(QCursor::pos()).x();
	double mY = ui.painterView->mapFromGlobal(QCursor::pos()).y();
	double rX = qrand() % w;
	double rY = qrand() % h;

	double x = 0;
	double y = 0;

	if( lastY == 0 )
	{
		// initial position #1
		x = mX - textSize;
		y = mY - textSize;
	}
	else if( mX != lastMX || mY != lastMY )
	{
		// initial position #2
		x = mX - textSize;
		y = mY - textSize;
	}
	else
	{
		// continuous position
		x = lastX;			
		y = lastY + textSize;
	}

	if( x == 0 || y == 0 || (y >= sH - textSize) || x >= sW )
	{
		// exception postion
		x = rX;
		y = rY;
	}

	lastX = x;
	lastY = y;
	lastMX = mX;
	lastMY = mY;

	return QPointF(x, y);
}

void SharedPainter::actionAddText( void )
{
	TextItemDialog dlg(this);
	int res = dlg.exec();
	if( res != QDialog::Accepted )
		return;

	boost::shared_ptr<CTextItem> textItem = boost::shared_ptr<CTextItem>(new CTextItem( dlg.text(), dlg.font(), dlg.textColor() ));
	textItem->setMyItem();

	QPointF pos = _calculateTextPos( textItem->font().pixelSize() );

	textItem->setPos( pos.x(), pos.y() );

	_requestAddItem( textItem );
}


void SharedPainter::actionPenWidth( void )
{
	bool ok = false;

	int width = QInputDialog::getInt(this, tr("Pen Width"),
                                  tr("Width:"), canvas_->penWidth(), 0, 100, 1, &ok);

	if ( !ok )
		return;

	canvas_->setPenSetting( canvas_->penColor(), width );
}


void SharedPainter::actionPenColor( void )
{
	QColor clr = QColorDialog::getColor(canvas_->penColor(), this, tr("Pen Color"));

	canvas_->setPenSetting( clr, canvas_->penWidth() );
}

void SharedPainter::actionPenMode( void )
{
	if( penModeAction_->isChecked() )
	{
		canvas_->setFreePenMode( true );
	}
	else
	{
		canvas_->setFreePenMode( false );
	}
}

void SharedPainter::actionScreenShot( void )
{
	screenShotMode_ = true;
	orgPos_ = pos();
	move(DEFAULT_HIDE_POS_X, DEFAULT_HIDE_POS_Y);
}


void SharedPainter::actionClearBGImage( void )
{
	SharePaintManagerPtr()->clearBackgroundImage();
}

void SharedPainter::actionClearScreen( void )
{
	SharePaintManagerPtr()->clearScreen();
}

void SharedPainter::actionUndo( void )
{
	SharePaintManagerPtr()->undoCommand();
}


void SharedPainter::actionBroadcastChannel( void )
{
	if( ! getBroadcastChannelString( true ) )
		return;	

	SharePaintManagerPtr()->setBroadCastChannel( SettingManagerPtr()->broadCastChannel() );
}

void SharedPainter::actionServerType( void )
{
	if( ! getBroadcastChannelString() )
		return;

	SharePaintManagerPtr()->startServer( SettingManagerPtr()->broadCastChannel() );
	setStatusBar_BroadCastType( tr("Server Type") );
}

void SharedPainter::actionClientType( void )
{
	if( ! getBroadcastChannelString() )
		return;

	if( SharePaintManagerPtr()->startClient() )
		setStatusBar_BroadCastType( tr("Client Type") );
	else
		setStatusBar_BroadCastType( tr("None Type") );
}

bool SharedPainter::getBroadcastChannelString( bool force )
{
	if( !force && SettingManagerPtr()->broadCastChannel().empty() == false )	// already setting
		return true;

	bool ok;
	QString channel = QInputDialog::getText(this, tr("Broadcast Channel"), tr("Channel: any string"), QLineEdit::Normal, SettingManagerPtr()->broadCastChannel().c_str(), &ok);
	if( ! ok )
		return false;

	if( channel.isEmpty() )
	{
		QMessageBox::warning(this, "", "Invalid channel string.");
		return false;
	}

	SettingManagerPtr()->setBroadCastChannel( channel.toStdString() );
	return true;
}

void SharedPainter::showEvent( QShowEvent * evt )
{
	int w = ui.painterView->width();
	int h = ui.painterView->height();
	canvas_->setSceneRect(0, 0, w, h);
}


void SharedPainter::closeEvent( QCloseEvent *evt )
{
	SettingManagerPtr()->save();
	SharePaintManagerPtr()->clearAllItems();

	QMainWindow::closeEvent( evt );
}


void SharedPainter::moveEvent( QMoveEvent * evt )
{
	// Screen Shot Action!
	if( screenShotMode_ )
	{
		if( evt->pos().x() == DEFAULT_HIDE_POS_X && evt->pos().y() == DEFAULT_HIDE_POS_Y )
		{
			// Screen Shot!
			QPixmap pixmap = QPixmap::grabWindow(QApplication::desktop()->winId());

			// Create Backgound Image Item
			boost::shared_ptr<CBackgroundImageItem> image = boost::shared_ptr<CBackgroundImageItem>( new CBackgroundImageItem );
			image->setPixmap( pixmap );
			image->setOwner( SharePaintManagerPtr()->myId() );
			image->setItemId( 0 );

			// Send to peers
			SharePaintManagerPtr()->sendBackgroundImage( image );

			// Restore original postion..
			move(orgPos_);
			screenShotMode_ = false;
		}
	}
}

void SharedPainter::resizeEvent( QResizeEvent *evt )
{
	int w = ui.painterView->width();
	int h = ui.painterView->height();

	canvas_->setSceneRect(0, 0, w, h);

	if( !resizeFreezingFlag_ )
		SharePaintManagerPtr()->notifyResizingMainWindow( width(), height() );

	QMainWindow::resizeEvent(evt);
}


void SharedPainter::_requestAddItem( boost::shared_ptr<CPaintItem> item )
{
	item->setOwner( SharePaintManagerPtr()->myId() );
	item->setItemId( currPaintItemId_++ );

	SharePaintManagerPtr()->sendPaintItem( item );
}


void SharedPainter::onICanvasViewEvent_BeginMove( CSharedPainterScene *view, boost::shared_ptr< CPaintItem > item )
{

}

void SharedPainter::onICanvasViewEvent_EndMove( CSharedPainterScene *view, boost::shared_ptr< CPaintItem > item )
{
	SharePaintManagerPtr()->notifyMoveItem( item );
}

void SharedPainter::onICanvasViewEvent_DrawItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item  )
{
	_requestAddItem( item );
}

void SharedPainter::onICanvasViewEvent_UpdateItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item )
{
	SharePaintManagerPtr()->notifyUpdateItem( item );
}

void SharedPainter::onICanvasViewEvent_RemoveItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item )
{
	SharePaintManagerPtr()->notifyRemoveItem( item );
}