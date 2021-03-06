#ifndef CSHAREDPAINTERSCENE_H
#define CSHAREDPAINTERSCENE_H

#include <QGraphicsScene>
#include "PaintItem.h"

class CSharedPainterScene;

class ICanvasViewEvent
{
public:
	virtual void onICanvasViewEvent_BeginMove( CSharedPainterScene *view, boost::shared_ptr< CPaintItem > item ) = 0;
	virtual void onICanvasViewEvent_EndMove( CSharedPainterScene *view, boost::shared_ptr< CPaintItem > item ) = 0;
	virtual void onICanvasViewEvent_DrawItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item ) = 0;
	virtual void onICanvasViewEvent_UpdateItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item ) = 0;
	virtual void onICanvasViewEvent_RemoveItem( CSharedPainterScene *view, boost::shared_ptr<CPaintItem> item ) = 0;
};

class CSharedPainterScene : public QGraphicsScene, public IGluePaintCanvas
{
	Q_OBJECT

public:
	static const int ZVALUE_NORMAL = 1;
	static const int ZVALUE_TOPMOST	= 9999;

	CSharedPainterScene(void);
	~CSharedPainterScene();

	void setEvent( ICanvasViewEvent *evt )
	{

		eventTarget_ = evt;
	}

	void setCursor( Qt::CursorShape shape )
	{
		//QApplication::setOverrideCursor( shape );

		QList<QGraphicsView *> list = views();
		for (int i = 0; i < list.size(); ++i)
		{
			list.at(i)->setCursor( shape );
		}
	}

	void setFreePenMode( bool enable ) 
	{
		if( enable )
			setCursor( Qt::ArrowCursor ); 
		else
			setCursor( Qt::PointingHandCursor ); 

		freePenMode_ = enable; 
	}
	void resetBackground( const QRectF &rect );
	void setPenSetting( const QColor &clr, int width )
	{
		penClr_ = clr;
		penWidth_ = width;
	}

	bool isFreePenMode( void ) { return freePenMode_; }

	int penWidth( void ) { return penWidth_; }
	const QColor & penColor( void ) { return penClr_; }

public:
	// IGluePaintCanvas
	virtual void moveItem( boost::shared_ptr<CPaintItem> item, double x, double y  );
	virtual void updateItem( boost::shared_ptr<CPaintItem> item );
	virtual void removeItem( CPaintItem * item );
	virtual void removeItem( boost::shared_ptr<CPaintItem> item );
	virtual void drawSendingStatus( boost::shared_ptr<CPaintItem> item );
	virtual void drawLine( boost::shared_ptr<CLineItem> line );
	virtual void drawFile( boost::shared_ptr<CFileItem> file );
	virtual void drawText( boost::shared_ptr<CTextItem> text );
	virtual void drawImage( boost::shared_ptr<CImageFileItem> image );
	virtual void drawBackgroundImage( boost::shared_ptr<CBackgroundImageItem> image );
	virtual void clearBackgroundImage( void );
	virtual void clearScreen( void )
	{
		currentZValue_ = ZVALUE_NORMAL;
		clearBackgroundImage();
	}

private slots:
	void sceneRectChanged(const QRectF &rect);

	// QGraphicsScene
private:
	void mouseMoveEvent( QGraphicsSceneMouseEvent *evt );
	void mousePressEvent( QGraphicsSceneMouseEvent *evt);
	void mouseReleaseEvent( QGraphicsSceneMouseEvent *evt );
	void drawBackground ( QPainter * painter, const QRectF & rect );
	void dragEnterEvent( QGraphicsSceneDragDropEvent * evt );
	void dragLeaveEvent( QGraphicsSceneDragDropEvent * evt );
	void dragMoveEvent( QGraphicsSceneDragDropEvent * evt );
	void dropEvent( QGraphicsSceneDragDropEvent * evt );

	// for CMyGraphicItem
public:	
	void onItemMoveBegin( boost::shared_ptr< CPaintItem > );
	void onItemMoveEnd( boost::shared_ptr< CPaintItem > );
	void onItemUpdate( boost::shared_ptr< CPaintItem > );
	void onItemRemove( boost::shared_ptr< CPaintItem > );

private:
	qreal currentZValue( void )
	{
		currentZValue_ += 0.01;
		return currentZValue_;
	}

	void addImageFileItem( const QPointF &pos, const QString &path );
	void addGeneralFileItem( const QPointF &pos, const QString &path );
	void resizeImage(QImage *image, const QSize &newSize);
	void drawLineTo( const QPointF &pt1, const QPointF &pt2, const QColor &clr, int width );
	void setScaleImageFileItem( boost::shared_ptr<CImageFileItem> image, QGraphicsPixmapItem *pixmapItem );
	void commonAddItem( QGraphicsItem *item );

	inline void fireEvent_DrawItem( boost::shared_ptr<CPaintItem> item )
	{
		if(eventTarget_)
			eventTarget_->onICanvasViewEvent_DrawItem( this, item );
	}

private:
	ICanvasViewEvent *eventTarget_;
	QColor penClr_;
	int penWidth_;
	QPointF prevPos_;
	bool drawFlag_;
	bool freePenMode_;
	QImage image_;
	QPixmap backgroundPixmap_;

	boost::shared_ptr<CBackgroundImageItem> backgroundImageItem_;
	boost::shared_ptr<CLineItem> currLineItem_;

	std::vector< QGraphicsLineItem * > tempLineItemList_;

	QFileIconProvider fileIconProvider_;
	qreal currentZValue_;
	qreal currentLineZValue_;
};

#endif // CSHAREDPAINTERSCENE_H
