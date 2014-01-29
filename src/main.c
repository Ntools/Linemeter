/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * main.c
 * Copyright (C) 2013 Nobby Noboru Hirano <nobby@ntools.net>
 * 
 * LineMeter is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * LineMeter is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <gtk/gtk.h>
#include <stdio.h>


#include <glib/gi18n.h>

#include <math.h>
#include <signal.h>
#include <pthread.h>
#include <usb.h>

#define USB_VENDOR          0x1352
#define USB_PID_IO_2_0      0x0120
#define USB_PID_IO_AKI      0x0121
#define USB_PID_FSIO_KIT    0x0110
#define USB_PID_FSIO30      0x0111

#define	DIA_STEP		24
#define	SPOOL_DIA		55.0
#define	SPOOL_LEN		(SPOOL_DIA * M_PI)


typedef struct _Private Private;
static struct _Private
{
	/* ANJUTA: Widgets declaration for linemeter.ui - DO NOT REMOVE */
};

static struct Private* priv = NULL;

typedef struct {
	struct usb_dev_handle *udev;
	GtkLabel *label;
	int polarity;
	int hup;
	int cnt;
	pthread_t thr;
	char meter[BUFSIZ];
} UPARAM;

static UPARAM *uparam = NULL;

/* For testing propose use the local (not installed) ui file */
/* #define UI_FILE PACKAGE_DATA_DIR"/ui/linemeter.ui" */
#ifdef DEBUG
#define UI_FILE "src/linemeter.ui"
#define TOP_WINDOW "window"
#else
#define UI_FILE "/usr/share/linemeter/ui/linemeter.ui"
#define TOP_WINDOW "linemeter"
#endif

/* Signal handlers */
/* Note: These may not be declared static because signal autoconnection
 * only works with non-static methods
 */

/* Called when the window is closed */
void
destroy (GtkWidget *widget, gpointer data)
{
	if(uparam->hup == 0) {
		uparam->hup = 1;
		pthread_join(uparam->thr,NULL);
	}

	gtk_main_quit ();
}

/* Called when the Close is clicked */
void
on_CloseBtn_clicked (GtkWidget *widget, gpointer data)
{
	if(uparam->hup == 0) {
		uparam->hup = 1;
		pthread_join(uparam->thr,NULL);
	}

	gtk_main_quit ();
}

/* Called when the Close is clicked */
void
on_ResetBtn_clicked (GtkWidget *widget, gpointer data)
{
	gtk_label_set_label(uparam->label, "   0.00");
	uparam->cnt = 0;
}

void
on_StartBtn_clicked1 (GtkWidget *widget, gpointer data)
{
	int usb_main(void);

	if(uparam->hup == 1) {
		uparam->label = GTK_LABEL(data);
		gtk_button_set_label (GTK_BUTTON(widget), "Stop");
		uparam->hup = 0;
		usb_main();
	} else {
		gtk_button_set_label (GTK_BUTTON(widget), "Start");

		uparam->hup = 1;
		pthread_join(uparam->thr,NULL);
	}
}

void
on_StartBtn_clicked2 (GtkWidget *widget, gpointer data)
{
	if(uparam->hup == 1)
		gtk_widget_set_sensitive (GTK_WIDGET(data), TRUE); 
	else
		gtk_widget_set_sensitive (GTK_WIDGET(data), FALSE);
}

void on_Polarity_toggled (GtkWidget *widget, gpointer data)
{
	uparam->polarity ^= 1;
}

static GtkWidget*
create_window (void)
{
	GtkWidget *window;
	GtkBuilder *builder;
	GError* error = NULL;

	/* Load UI from file */
	builder = gtk_builder_new ();
	if (!gtk_builder_add_from_file (builder, UI_FILE, &error))
	{
		g_critical ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
	}

	/* Auto-connect signal handlers */
	gtk_builder_connect_signals (builder, NULL);

	/* Get the window object from the ui file */
	window = GTK_WIDGET (gtk_builder_get_object (builder, TOP_WINDOW));
        if (!window)
        {
                g_critical ("Widget \"%s\" is missing in file %s.",
				TOP_WINDOW,
				UI_FILE);
        }

	/* ANJUTA: Widgets initialization for linemeter.ui - DO NOT REMOVE */
	priv = g_malloc (sizeof (struct _Private));

	uparam = calloc (1, sizeof (UPARAM));
	uparam->hup = 1;

	g_object_unref (builder);
	
	return window;
}


/* Init USB */
struct usb_bus *io_init()
{
    usb_init();
    usb_find_busses();
    usb_find_devices();
    return( usb_get_busses() );
}

/* Find Km2Net USB-IO Family */
struct usb_device *io_find(struct usb_bus *busses)
{
	struct usb_bus *bus;
	struct usb_device *dev;

    for(bus=busses; bus != NULL; bus=bus->next){
        for(dev=bus->devices; dev != NULL; dev = dev->next) {
            if ( dev->descriptor.idVendor  == USB_VENDOR
            &&   (   dev->descriptor.idProduct == USB_PID_IO_2_0
                  || dev->descriptor.idProduct == USB_PID_IO_AKI
                  || dev->descriptor.idProduct == USB_PID_FSIO_KIT
                  || dev->descriptor.idProduct == USB_PID_FSIO30) ) {
				printf("Vendor ID = 0x%02x Product ID = 0x%02x.\n", 
				   dev->descriptor.idVendor, dev->descriptor.idProduct);
                return( dev );
            }
        }
    }
    return( NULL );
}

/* USBIO Open */
struct usb_dev_handle *io_open(struct usb_device *dev)
{
    struct usb_dev_handle *udev = NULL;

    if( (udev=usb_open(dev))==NULL ){
        printf("usb_open Error.(%s)\n",usb_strerror());
        exit(1);
    }

    if( usb_set_configuration(udev,dev->config->bConfigurationValue)<0 ){
        if( usb_detach_kernel_driver_np(udev,dev->config->interface->altsetting->bInterfaceNumber)<0 ){
            printf("usb_set_configuration Error.\n");
            printf("usb_detach_kernel_driver_np Error.(%s)\n",usb_strerror());
        }
    }

    if( usb_claim_interface(udev,dev->config->interface->altsetting->bInterfaceNumber)<0 ){
        if( usb_detach_kernel_driver_np(udev,dev->config->interface->altsetting->bInterfaceNumber)<0 ){
            printf("usb_claim_interface Error.\n");
            printf("usb_detach_kernel_driver_np Error.(%s)\n",usb_strerror());
        }
    }

    if( usb_claim_interface(udev,dev->config->interface->altsetting->bInterfaceNumber)<0 ){
        printf("usb_claim_interface Error.(%s)\n",usb_strerror());
    }

    return(udev);
}

/* USBIO Close */
void io_close(struct usb_dev_handle *udev)
{
	usb_reset(udev);
    if( usb_close(udev)<0 ){
        printf("usb_close Error.(%s)\n",usb_strerror());
    }
}

/* USB-IO Family Send Recive */
int io_send_recv(struct usb_dev_handle *udev, unsigned char *sendData, unsigned char *recvData )
{
    static unsigned char seq = 0;
    int ret;
    int i;

    seq++;
    sendData[63] = seq;

    ret = usb_bulk_write(udev, 1, (char *)sendData, 64, 1000);
    if( ret < 0 ){
        printf("Send-Error (%d)\n", ret);
        return (-1);
    }
    if(ret != 64){
        printf("Send-Len Error %d\n", ret);
        return (-2);
    }

    for (i=0; i<20; i++) { 
        ret = usb_bulk_read(udev, 1, (char *)recvData, 64, 100);
        if (ret == 64 && sendData[63] == recvData[63]) {
            //Read OK!
            return (0);
        }
		printf("seq = %02x, recData = %02x\n", seq, recvData[63]);
		return -3;
        printf("Read Skip(%d)\n",ret);
        usleep(1000);
    }

    printf("Read Data Error\n");
    return (-4);
}

void *usb_loop(void* pParam)
{
	int i = 0;
	unsigned char odat = 0xf;
    unsigned char sendData[64];
    unsigned char recvData[64];

	usb_dev_handle    *udev = pParam;

	while(uparam->hup == 0) {
		/*------------------------*/
		/* Port 1,2 in_out        */
		/*------------------------*/
		memset(sendData, 0, sizeof(sendData));
		sendData[0] = 0x20;     //in out
		sendData[1] = 0x01;     //port1
		sendData[2] = 0x0f;     //out 0xf
		sendData[3] = 0x02;     //port2
		sendData[4] = 0x0f;     //out 0x0f
		if(io_send_recv(udev,sendData,recvData) < 0) break;
// data check
		if((recvData[2] ^ odat) & 1 && odat & 1) {
			int ad;
			ad = (uparam->polarity) ? -1: 1;
			if (!(recvData[2] & 2)) uparam->cnt += ad;
			else uparam->cnt -= ad;
		}
		odat = recvData[2];
		++i;
		if(i > 120) {
			float met = SPOOL_LEN * uparam->cnt / DIA_STEP / 1000.0 ;

			sprintf(uparam->meter, "%4.2f", met);
			gtk_label_set_label(uparam->label, uparam->meter);
			i = 0;
		}
	}
    /*---------------------*/
    /* Device Close & Exit */
    /*---------------------*/
    io_close(udev);
	putchar('\n');
	return(uparam);
}

int usb_main(void)
{
    struct usb_bus    *bus;
    struct usb_device *dev;
    usb_dev_handle    *udev;

    /*-------------*/
    /* Device Open */
    /*-------------*/
    bus=io_init();
    dev=io_find(bus);
    if( dev==NULL ){ 
        puts("io_find NG");
        exit(1);
    }
    
    udev=io_open(dev);
    if( udev==NULL ){
        puts("io_open NG");
        exit(2);
    }
	uparam->udev = udev;

#if 0
	usb_loop(udev);
#else
	pthread_create(&uparam->thr, NULL, usb_loop, udev);
#endif

	return 0;
}


int
main (int argc, char *argv[])
{
 	GtkWidget *window;


#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif


	gtk_init (&argc, &argv);

	window = create_window ();
	gtk_widget_show (window);

	gtk_main ();

	g_free (priv);

	return 0;
}
