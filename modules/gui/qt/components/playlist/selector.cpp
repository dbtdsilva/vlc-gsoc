/*****************************************************************************
 * selector.cpp : Playlist source selector
 ****************************************************************************
 * Copyright (C) 2006-2009 the VideoLAN team
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"
#include "components/playlist/selector.hpp"
#include "playlist_model.hpp"                /* plMimeData */
#include "input_manager.hpp"                 /* MainInputManager, for podcast */

#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QDragMoveEvent>
#include <QTreeWidgetItem>
#include <QHBoxLayout>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QResource>
#include <QComboBox>
#include <QDialogButtonBox>
#include <assert.h>

#include <vlc_playlist.h>
#include <vlc_services_discovery.h>

void SelectorActionButton::paintEvent( QPaintEvent *event )
{
    QPainter p( this );
    QColor color = palette().color( QPalette::HighlightedText );
    color.setAlpha( 80 );
    if( underMouse() )
        p.fillRect( rect(), color );
    p.setPen( color );
    int frame = style()->pixelMetric( QStyle::PM_DefaultFrameWidth, 0, this );
    p.drawLine( rect().topLeft() + QPoint( 0, frame ),
                rect().bottomLeft() - QPoint( 0, frame ) );
    QFramelessButton::paintEvent( event );
}

PLSelItem::PLSelItem ( QTreeWidgetItem *i, const QString& text )
    : qitem(i), lblAction( NULL), pInnerTree( NULL )
{
    layout = new QHBoxLayout( this );
    layout->setContentsMargins(0,0,0,0);
    layout->addSpacing( 3 );

    lbl = new QElidingLabel( text );
    layout->addWidget(lbl, 1);

    int height = qMax( 22, fontMetrics().height() + 8 );
    setMinimumHeight( height );
}

PLSelItem::~PLSelItem()
{
    if ( pInnerTree != NULL )
        delete pInnerTree;
}

void PLSelItem::createInnerTree( const char* slot_add, const char* slot_remove,
        const char* slot_activate )
{
    QTreeWidgetItem* parent = this->treeItem();
    pInnerTree = new PLSelItemTree(parent, slot_add, slot_remove,
            slot_activate);
    parent->setChildIndicatorPolicy(
            QTreeWidgetItem::ChildIndicatorPolicy::ShowIndicator);
}

void PLSelItem::mouseReleaseEvent( QMouseEvent* event )
{
    // Emit an internal signal and re-invoke the original callback
    emit subTreeActivated( this );
    QWidget::mouseReleaseEvent( event );
}

void PLSelItem::addAction( ItemAction act, const QString& tooltip )
{
    if( lblAction ) return; //might change later

    QIcon icon;

    switch( act )
    {
    case ADD_ACTION:
        icon = QIcon( ":/buttons/playlist/playlist_add" ); break;
    case RM_ACTION:
        icon = QIcon( ":/buttons/playlist/playlist_remove" ); break;
    default:
        return;
    }

    lblAction = new SelectorActionButton();
    lblAction->setIcon( icon );
    lblAction->setMinimumWidth( lblAction->sizeHint().width() + 6 );

    if( !tooltip.isEmpty() ) lblAction->setToolTip( tooltip );

    layout->addWidget( lblAction, 0 );
    lblAction->hide();

    CONNECT( lblAction, clicked(), this, triggerAction() );
}


PLSelector::PLSelector( QWidget *p, intf_thread_t *_p_intf )
           : QTreeWidget( p ), p_intf(_p_intf)
{
    /* Properties */
    setFrameStyle( QFrame::NoFrame );
    setAttribute( Qt::WA_MacShowFocusRect, false );
    viewport()->setAutoFillBackground( false );
    setIconSize( QSize( 24,24 ) );
    setIndentation( 12 );
    setHeaderHidden( true );
    setRootIsDecorated( true );
    setAlternatingRowColors( false );

    /* drops */
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
    invisibleRootItem()->setFlags( invisibleRootItem()->flags() & ~Qt::ItemIsDropEnabled );

#ifdef Q_OS_MAC
    setAutoFillBackground( true );
    QPalette palette;
    palette.setColor( QPalette::Window, QColor(209,215,226) );
    setPalette( palette );
#endif
    setMinimumHeight( 120 );

    /* Podcast connects */
    CONNECT( THEMIM, playlistItemAppended( int, int ),
             this, plItemAdded( int, int ) );
    CONNECT( THEMIM, playlistItemRemoved( int ),
             this, plItemRemoved( int ) );
    DCONNECT( THEMIM->getIM(), metaChanged( input_item_t *),
              this, inputItemUpdate( input_item_t * ) );

    createItems();

    setRootIsDecorated( false );
    setIndentation( 5 );
    /* Expand at least to show level 2 */
    for ( int i = 0; i < topLevelItemCount(); i++ )
        expandItem( topLevelItem( i ) );

    /***
     * We need to react to both clicks and activation (enter-key) here.
     * We use curItem to avoid rebuilding twice.
     * See QStyle::SH_ItemView_ActivateItemOnSingleClick
     ***/
    curItem = NULL;
    CONNECT( this, itemActivated( QTreeWidgetItem *, int ),
             this, setSource( QTreeWidgetItem *) );
    CONNECT( this, itemClicked( QTreeWidgetItem *, int ),
             this, setSource( QTreeWidgetItem *) );
    CONNECT( this, itemExpanded( QTreeWidgetItem * ),
             this, setSource( QTreeWidgetItem *) );
}

PLSelector::~PLSelector()
{
    for ( auto& lItem : listItems )
    {
        if (lItem->innerTree() == NULL)
            continue;

        int c = lItem->treeItem()->childCount();
        for( int i = 0; i < c; i++ )
        {
            QTreeWidgetItem *item = lItem->treeItem()->child(i);
            input_item_t *p_input = item->data( 0, IN_ITEM_ROLE ).value<input_item_t*>();
            input_item_Release( p_input );
        }
    }

    for (std::vector<PLSelItem*>::iterator it = listItems.begin();
            it != listItems.end(); ++it)
        delete *it;
}

PLSelItem * putSDData( PLSelItem* item, const char* name, const char* longname )
{
    item->treeItem()->setData( 0, NAME_ROLE, qfu( name ) );
    item->treeItem()->setData( 0, LONGNAME_ROLE, qfu( longname ) );
    return item;
}

PLSelItem * putPLData( PLSelItem* item, playlist_item_t* plItem )
{
    item->treeItem()->setData( 0, PL_ITEM_ROLE, QVariant::fromValue( plItem ) );
/*    item->setData( 0, PL_ITEM_ID_ROLE, plItem->i_id );
    item->setData( 0, IN_ITEM_ROLE, QVariant::fromValue( (void*) plItem->p_input ) ); );*/
    return item;
}

/*
 * Reads and updates the playlist's duration as [xx:xx] after the label in the tree
 * item - the treeview item to get the duration for
 * prefix - the string to use before the time (should be the category name)
 */
void PLSelector::updateTotalDuration( PLSelItem* item, const char* prefix )
{
    /* Getting  the playlist */
    QVariant playlistVariant = item->treeItem()->data( 0, PL_ITEM_ROLE );
    playlist_item_t* node = playlistVariant.value<playlist_item_t*>();

    /* Get the duration of the playlist item */
    playlist_Lock( THEPL );
    mtime_t mt_duration = playlist_GetNodeDuration( node );
    playlist_Unlock( THEPL );

    /* Formatting time */
    QString qs_timeLabel( prefix );

    int i_seconds = mt_duration / 1000000;
    int i_minutes = i_seconds / 60;
    i_seconds = i_seconds % 60;
    if( i_minutes >= 60 )
    {
        int i_hours = i_minutes / 60;
        i_minutes = i_minutes % 60;
        qs_timeLabel += QString(" [%1:%2:%3]").arg( i_hours ).arg( i_minutes, 2, 10, QChar('0') ).arg( i_seconds, 2, 10, QChar('0') );
    }
    else
        qs_timeLabel += QString( " [%1:%2]").arg( i_minutes, 2, 10, QChar('0') ).arg( i_seconds, 2, 10, QChar('0') );

    item->setText( qs_timeLabel );
}

void PLSelector::createItems()
{
    /* PL */
    playlistItem = putPLData( addItem( PL_ITEM_TYPE, N_("Playlist"), true ),
                              THEPL->p_playing );
    playlistItem->treeItem()->setData( 0, SPECIAL_ROLE, QVariant( IS_PL ) );
    playlistItem->treeItem()->setData( 0, Qt::DecorationRole, QIcon( ":/sidebar/playlist" ) );
    setCurrentItem( playlistItem->treeItem() );

    /* ML */
    if( THEPL->p_media_library )
    {
        PLSelItem *ml = putPLData( addItem( PL_ITEM_TYPE, N_("Media Library"), true ),
          THEPL->p_media_library );
        ml->treeItem()->setData( 0, SPECIAL_ROLE, QVariant( IS_ML ) );
        ml->treeItem()->setData( 0, Qt::DecorationRole, QIcon( ":/sidebar/library" ) );
    }

    /* SD nodes */
    QTreeWidgetItem *mycomp = addItem( CATEGORY_TYPE, N_("My Computer"), false, true )->treeItem();
    QTreeWidgetItem *devices = addItem( CATEGORY_TYPE, N_("Devices"), false, true )->treeItem();
    QTreeWidgetItem *lan = addItem( CATEGORY_TYPE, N_("Local Network"), false, true )->treeItem();
    QTreeWidgetItem *internet = addItem( CATEGORY_TYPE, N_("Internet"), false, true )->treeItem();

#define NOT_SELECTABLE(w) w->setFlags( w->flags() ^ Qt::ItemIsSelectable );
    NOT_SELECTABLE( mycomp );
    NOT_SELECTABLE( devices );
    NOT_SELECTABLE( lan );
    NOT_SELECTABLE( internet );
#undef NOT_SELECTABLE

    /* SD subnodes */
    char **ppsz_longnames;
    int *p_categories;
    char **ppsz_names = vlc_sd_GetNames( THEPL, &ppsz_longnames, &p_categories );
    if( !ppsz_names )
        return;

    char **ppsz_name = ppsz_names, **ppsz_longname = ppsz_longnames;
    int *p_category = p_categories;
    for( ; *ppsz_name; ppsz_name++, ppsz_longname++, p_category++ )
    {
        //msg_Dbg( p_intf, "Adding a SD item: %s", *ppsz_longname );

        PLSelItem *selItem;
        QIcon icon;
        QString name( *ppsz_name );
        switch( *p_category )
        {
        case SD_CAT_INTERNET:
            {
            selItem = addItem( SD_TYPE, *ppsz_longname, false, false, internet );
            if( name.startsWith( "podcast" ) )
            {
                selItem->createInnerTree(SLOT(podcastAdd(PLSelItem*)),
                                         SLOT(podcastRemove(PLSelItem*)),
                                         nullptr);
                icon = QIcon( ":/sidebar/podcast" );
            }
            else if ( name.startsWith( "cloudstorage" ))
            {
                selItem->createInnerTree(SLOT(cloudProviderAdd(PLSelItem*)),
                                         SLOT(cloudProviderRemove(PLSelItem*)),
                                         SLOT(cloudProviderActivated(PLSelItem*)));
                icon = QIcon( ":/sidebar/cloud" );
            }
            else if ( name.startsWith( "lua{" ) )
            {
                int i_head = name.indexOf( "sd='" ) + 4;
                int i_tail = name.indexOf( '\'', i_head );
                QString iconname = QString( ":/sidebar/sd/%1" ).arg( name.mid( i_head, i_tail - i_head ) );
                QResource resource( iconname );
                if ( !resource.isValid() )
                    icon = QIcon( ":/sidebar/network" );
                else
                    icon = QIcon( iconname );
            }
            }
            break;
        case SD_CAT_DEVICES:
            name = name.mid( 0, name.indexOf( '{' ) );
            selItem = addItem( SD_TYPE, *ppsz_longname, false, false, devices );
            if ( name == "xcb_apps" )
                icon = QIcon( ":/sidebar/screen" );
            else if ( name == "mtp" )
                icon = QIcon( ":/sidebar/mtp" );
            else if ( name == "disc" )
                icon = QIcon( ":/sidebar/disc" );
            else
                icon = QIcon( ":/sidebar/capture" );
            break;
        case SD_CAT_LAN:
            selItem = addItem( SD_TYPE, *ppsz_longname, false, false, lan );
            icon = QIcon( ":/sidebar/lan" );
            break;
        case SD_CAT_MYCOMPUTER:
            name = name.mid( 0, name.indexOf( '{' ) );
            selItem = addItem( SD_TYPE, *ppsz_longname, false, false, mycomp );
            if ( name == "video_dir" )
                icon = QIcon( ":/sidebar/movie" );
            else if ( name == "audio_dir" )
                icon = QIcon( ":/sidebar/music" );
            else if ( name == "picture_dir" )
                icon = QIcon( ":/sidebar/pictures" );
            else
                icon = QIcon( ":/sidebar/movie" );
            break;
        default:
            selItem = addItem( SD_TYPE, *ppsz_longname );
        }

        if (selItem->innerTree() != NULL &&
            selItem->innerTree()->slotAddFunct() != NULL)
        {
            selItem->addAction( ADD_ACTION, qtr( "Add a new item" ) );
            connect( selItem, SIGNAL(action( PLSelItem* )),
                     this, selItem->innerTree()->slotAddFunct() );
        }

        selItem->treeItem()->setData( 0, SD_CATEGORY_ROLE, *p_category );
        putSDData( selItem, *ppsz_name, *ppsz_longname );
        if ( ! icon.isNull() )
            selItem->treeItem()->setData( 0, Qt::DecorationRole, icon );

        free( *ppsz_name );
        free( *ppsz_longname );

        listItems.push_back( selItem );
    }
    free( ppsz_names );
    free( ppsz_longnames );
    free( p_categories );

    if( mycomp->childCount() == 0 ) delete mycomp;
    if( devices->childCount() == 0 ) delete devices;
    if( lan->childCount() == 0 ) delete lan;
    if( internet->childCount() == 0 ) delete internet;
}

void PLSelector::setSource( QTreeWidgetItem *item )
{
    if( !item || item == curItem )
        return;

    bool b_ok;
    int i_type = item->data( 0, TYPE_ROLE ).toInt( &b_ok );
    if( !b_ok || i_type == CATEGORY_TYPE )
        return;

    bool sd_loaded;
    playlist_item_t *pl_item = NULL;
    if( i_type == SD_TYPE )
    {
        QString qs = item->data( 0, NAME_ROLE ).toString();
        sd_loaded = playlist_IsServicesDiscoveryLoaded( THEPL, qtu( qs ) );
        if( !sd_loaded )
        {
            if ( playlist_ServicesDiscoveryAdd( THEPL, qtu( qs ) ) != VLC_SUCCESS )
                return ;

            services_discovery_descriptor_t test;

            if ( playlist_ServicesDiscoveryControl( THEPL, qtu( qs ),
                                                    SD_CMD_DESCRIPTOR, &test ) == VLC_SUCCESS )
            {
                item->setData( 0, CAP_SEARCH_ROLE, (test.i_capabilities & SD_CAP_SEARCH) );
            }
        }

        playlist_Lock( THEPL );
        /* Find the right item for the SD */
        /* FIXME: searching by name - what could possibly go wrong? */
        pl_item = playlist_ChildSearchName( &(THEPL->root),
            vlc_gettext(qtu(item->data(0, LONGNAME_ROLE).toString())) );

        PLSelItem* sel_item = itemWidget( item );
        if (sel_item != NULL && sel_item->innerTree() != NULL)
        {
            if ( pl_item && !sd_loaded )
            {
                sel_item->innerTree()->setParentId( pl_item->i_id );
                for ( int i = 0; i < pl_item->i_children; i++ )
                    addItemOnTree( pl_item->pp_children[i], sel_item );
            }
            pl_item = NULL; // prevent sidebar item activation
        }
        playlist_Unlock( THEPL );
    }
    else
    {
        pl_item = item->data( 0, PL_ITEM_ROLE ).value<playlist_item_t*>();
    }

    curItem = item;

    if( pl_item )
    {
        emit categoryActivated( pl_item, false );
        int i_cat = item->data( 0, SD_CATEGORY_ROLE ).toInt();
        emit SDCategorySelected( i_cat == SD_CAT_INTERNET
                                 || i_cat == SD_CAT_LAN );
    }
}

PLSelItem * PLSelector::addItem (
    SelectorItemType type, const char* str, bool drop, bool bold,
    QTreeWidgetItem* parentItem )
{
  QTreeWidgetItem *item = parentItem ?
      new QTreeWidgetItem( parentItem ) : new QTreeWidgetItem( this );

  PLSelItem *selItem = new PLSelItem( item, qtr( str ) );
  if ( bold ) selItem->setStyleSheet( "font-weight: bold;" );
  setItemWidget( item, 0, selItem );
  item->setData( 0, TYPE_ROLE, (int)type );
  if( !drop ) item->setFlags( item->flags() & ~Qt::ItemIsDropEnabled );

  return selItem;
}

PLSelItem *PLSelector::addItemOnTree( playlist_item_t *p_item, PLSelItem* sel_item )
{
    input_item_Hold( p_item->p_input );

    char *psz_name = input_item_GetName( p_item->p_input );
    QTreeWidgetItem* parent = sel_item->innerTree()->parent();
    PLSelItem *item = addItem( PL_ITEM_TYPE,  psz_name, false, false, parent );
    free( psz_name );

    item->treeItem()->setData( 0, PL_ITEM_ROLE, QVariant::fromValue( p_item ) );
    item->treeItem()->setData( 0, PL_ITEM_ID_ROLE, QVariant(p_item->i_id) );
    item->treeItem()->setData( 0, IN_ITEM_ROLE, QVariant::fromValue( p_item->p_input ) );

    if (sel_item->innerTree()->slotActivatedFunct() != nullptr)
    {
        connect( item, SIGNAL(subTreeActivated( PLSelItem* )),
                 this, sel_item->innerTree()->slotActivatedFunct() );
    }

    if (sel_item->innerTree()->slotRemoveFunct() != NULL)
    {
        item->addAction( RM_ACTION, qtr( "Remove item" ) );
        connect( item, SIGNAL(action( PLSelItem* )),
                 this, sel_item->innerTree()->slotRemoveFunct() );
    }
    return item;
}

QStringList PLSelector::mimeTypes() const
{
    QStringList types;
    types << "vlc/qt-input-items";
    return types;
}

bool PLSelector::dropMimeData ( QTreeWidgetItem * parent, int,
    const QMimeData * data, Qt::DropAction )
{
    if( !parent ) return false;

    QVariant type = parent->data( 0, TYPE_ROLE );
    if( type == QVariant() ) return false;

    int i_truth = parent->data( 0, SPECIAL_ROLE ).toInt();
    if( i_truth != IS_PL && i_truth != IS_ML ) return false;

    bool to_pl = ( i_truth == IS_PL );

    const PlMimeData *plMimeData = qobject_cast<const PlMimeData*>( data );
    if( !plMimeData ) return false;

    QList<input_item_t*> inputItems = plMimeData->inputItems();

    playlist_Lock( THEPL );

    foreach( input_item_t *p_input, inputItems )
    {
        playlist_item_t *p_item = playlist_ItemGetByInput( THEPL, p_input );
        if( !p_item ) continue;

        playlist_NodeAddCopy( THEPL, p_item,
                              to_pl ? THEPL->p_playing : THEPL->p_media_library,
                              PLAYLIST_END );
    }

    playlist_Unlock( THEPL );

    return true;
}

void PLSelector::dragMoveEvent ( QDragMoveEvent * event )
{
    event->setDropAction( Qt::CopyAction );
    QAbstractItemView::dragMoveEvent( event );
}

void PLSelector::plItemAdded( int item, int parent )
{
    updateTotalDuration(playlistItem, "Playlist");

    playlist_Lock( THEPL );

    playlist_item_t *p_item = playlist_ItemGetById( THEPL, item );
    if( !p_item )
    {
        playlist_Unlock( THEPL );
        return;
    }

    PLSelItem* sel_item = NULL;

    for (std::vector<PLSelItem*>::iterator it = listItems.begin();
            it != listItems.end(); ++it)
    {
        PLSelItem* lItem = *it;
        if (lItem->innerTree() == NULL || lItem->innerTree()->parentId()
                != parent)
            continue;
        sel_item = lItem;
    }

    if ( sel_item != NULL )
    {
        // Check if the item already exists on the tree
        int childsCounter = sel_item->treeItem()->childCount();
        for( int i = 0; i < childsCounter; i++ )
        {
            QTreeWidgetItem *widgetItem = sel_item->treeItem()->child(i);
            if( widgetItem->data( 0, PL_ITEM_ID_ROLE ).toInt() == item )
            {
                // Item already exists
                playlist_Unlock( THEPL );
                return;
            }
        }
        // The items does not exists yet
        addItemOnTree( p_item, sel_item );
        sel_item->treeItem()->setExpanded( true );
    }

    playlist_Unlock( THEPL );
}

void PLSelector::plItemRemoved( int id )
{
    updateTotalDuration(playlistItem, "Playlist");

    for (std::vector<PLSelItem*>::iterator it = listItems.begin();
            it != listItems.end(); ++it)
    {
        PLSelItem* lItem = *it;
        if (lItem->innerTree() == NULL)
            continue;

        int c = lItem->treeItem()->childCount();
        for( int i = 0; i < c; i++ )
        {
            QTreeWidgetItem *item = lItem->treeItem()->child(i);
            if( item->data( 0, PL_ITEM_ID_ROLE ).toInt() == id )
            {
                input_item_t *p_input = item->data( 0, IN_ITEM_ROLE ).value<input_item_t*>();
                //msg_Dbg( p_intf, "Removing podcast: (%d) %s", id, p_input->psz_uri );
                input_item_Release( p_input );
                delete item;
                return;
            }
        }
    }
}

void PLSelector::inputItemUpdate( input_item_t *arg )
{
    updateTotalDuration(playlistItem, "Playlist");

    for (std::vector<PLSelItem*>::iterator it = listItems.begin();
            it != listItems.end(); ++it)
    {
        PLSelItem* lItem = *it;
        if (lItem->innerTree() == NULL)
            continue;

        int c = lItem->treeItem()->childCount();
        for( int i = 0; i < c; i++ )
        {
            QTreeWidgetItem *item = lItem->treeItem()->child(i);
            input_item_t *p_input = item->data( 0, IN_ITEM_ROLE ).value<input_item_t*>();
            if( p_input == arg )
            {
                PLSelItem *si = itemWidget( item );
                char *psz_name = input_item_GetName( p_input );
                si->setText( qfu( psz_name ) );
                free( psz_name );
                return;
            }
        }
    }
}

void PLSelector::podcastAdd( PLSelItem * item)
{
    //assert( podcastsParent );

    bool ok;
    QString url = QInputDialog::getText( this, qtr( "Subscribe" ),
                                         qtr( "Enter URL of the podcast to subscribe to:" ),
                                         QLineEdit::Normal, QString(), &ok );
    if( !ok || url.isEmpty() ) return;

    //to load the SD in case it's not loaded
    setSource( item->innerTree()->parent() );

    QString request("ADD:");
    request += url.trimmed();
    var_SetString( THEPL, "podcast-request", qtu( request ) );
}

void PLSelector::podcastRemove( PLSelItem* item )
{
    QString question ( qtr( "Do you really want to unsubscribe from %1?" ) );
    question = question.arg( item->text() );
    QMessageBox::StandardButton res =
        QMessageBox::question( this, qtr( "Unsubscribe" ), question,
                               QMessageBox::Yes | QMessageBox::No,
                               QMessageBox::No );
    if( res == QMessageBox::No ) return;

    input_item_t *input = item->treeItem()->data( 0, IN_ITEM_ROLE ).value<input_item_t*>();
    if( !input ) return;

    QString request("RM:");
    char *psz_uri = input_item_GetURI( input );
    request += qfu( psz_uri );
    var_SetString( THEPL, "podcast-request", qtu( request ) );
    free( psz_uri );
}

void PLSelector::cloudProviderAdd( PLSelItem * item )
{
    QDialog *dialog = new QDialog( this );
    dialog->setWindowTitle( "Cloud Storage Provider" );
    QVBoxLayout *vbox = new QVBoxLayout();
    QLabel *label = new QLabel( "Select the provider to authenticate", d );

    QComboBox *providers_box = new QComboBox();
    QStringList items;
    items << "google" << "onedrive" << "dropbox" << "box";
    items << "amazons3" << "yandex" << "owncloud";
    providers_box->addItems( items );
    // Disabled providers
    //items << "youtube" << "mega" << "amazon";

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    QObject::connect(buttonBox, SIGNAL(accepted()), d, SLOT(accept()));
    QObject::connect(buttonBox, SIGNAL(rejected()), d, SLOT(reject()));

    vbox->addWidget(label);
    vbox->addWidget(providers_box);
    vbox->addWidget(buttonBox);
    dialog->setLayout(vbox);

    int result = dialog->exec();
    QString provider = providers_box->currentText();
    if ( result != QDialog::Accepted || provider.isEmpty() )
        return;

    //to load the SD in case it's not loaded
    setSource( item->innerTree()->parent() );

    QString request( "ADD:" + provider );
    var_SetString( THEPL, "cloudstorage-request", qtu( request ) );
}

void PLSelector::cloudProviderRemove( PLSelItem* item )
{
    QString question ( qtr( "Do you really want to remove the service %1?" ) );
    question = question.arg( item->text() );
    QMessageBox::StandardButton res =
        QMessageBox::question( this, qtr( "Logout" ), question,
                               QMessageBox::Yes | QMessageBox::No,
                               QMessageBox::No );
    if( res == QMessageBox::No ) return;

    input_item_t *input = item->treeItem()->data( 0, IN_ITEM_ROLE ).
            value<input_item_t*>();
    if( !input ) return;

    QString request("RM:");
    request += qfu( input->psz_name );
    var_SetString( THEPL, "cloudstorage-request", qtu( request ) );
}

void PLSelector::cloudProviderActivated( PLSelItem* item )
{
    input_item_t *input = item->treeItem()->data( 0, IN_ITEM_ROLE ).
            value<input_item_t*>();
    if( !input ) return;

    QString request("ACT:");
    request += qfu( input->psz_name );
    var_SetString( THEPL, "cloudstorage-request", qtu( request ) );
}

PLSelItem * PLSelector::itemWidget( QTreeWidgetItem *item )
{
    return ( static_cast<PLSelItem*>( QTreeWidget::itemWidget( item, 0 ) ) );
}

void PLSelector::drawBranches ( QPainter * painter, const QRect & rect, const QModelIndex & index ) const
{
    if( !model()->hasChildren( index ) ) return;
    QStyleOption option;
    option.initFrom( this );
    option.rect = rect.adjusted( rect.width() - indentation(), 0, 0, 0 );
    style()->drawPrimitive( isExpanded( index ) ?
                            QStyle::PE_IndicatorArrowDown :
                            QStyle::PE_IndicatorArrowRight, &option, painter );
}

void PLSelector::getCurrentItemInfos( int* type, bool* can_delay_search, QString *string)
{
    *type = currentItem()->data( 0, TYPE_ROLE ).toInt();
    *string = currentItem()->data( 0, NAME_ROLE ).toString();
    *can_delay_search = currentItem()->data( 0, CAP_SEARCH_ROLE ).toBool();
}

int PLSelector::getCurrentItemCategory()
{
    return currentItem()->data( 0, SPECIAL_ROLE ).toInt();
}

void PLSelector::wheelEvent( QWheelEvent *e )
{
    if( verticalScrollBar()->isVisible() && (
        (verticalScrollBar()->value() != verticalScrollBar()->minimum() && e->delta() >= 0 ) ||
        (verticalScrollBar()->value() != verticalScrollBar()->maximum() && e->delta() < 0 )
        ) )
        QApplication::sendEvent(verticalScrollBar(), e);

    // Accept this event in order to prevent unwanted volume up/down changes
    e->accept();
}

PLSelItemTree::PLSelItemTree(QTreeWidgetItem* parent,
            const char* slot_add_funct, const char* slot_remove_funct,
            const char* slot_activate_funct) :
        parent_ptr(parent), parent_id(-1), slot_add_funct(slot_add_funct),
        slot_remove_funct(slot_remove_funct),
        slot_activate_funct(slot_activate_funct)
{
}
