#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>

#include "kiosk-input-selector-application.h"

int
main (int    argc,
      char **argv)
{
        g_autoptr (KioskInputSelectorApplication) application = NULL;

        application = kiosk_input_selector_application_new ();

        g_application_run (G_APPLICATION (application), argc, argv);

        return 0;
}
