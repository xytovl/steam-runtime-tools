/*
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <libglnx.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <linux/input.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-report-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "mock-input-device.h"
#include "test-utils.h"

static const char *argv0;

typedef struct
{
  enum { MOCK, DIRECT, UDEV } type;
} Config;

static const Config defconfig =
{
  .type = MOCK,
};

static const Config direct_config =
{
  .type = DIRECT,
};

static const Config udev_config =
{
  .type = UDEV,
};

typedef struct
{
  const Config *config;
  gchar *srcdir;
  gchar *builddir;
  GMainContext *monitor_context;
  GPtrArray *log;
  gboolean skipped;
} Fixture;

static void
setup (Fixture *f,
       gconstpointer context)
{
  const Config *config = context;
  GStatBuf sb;

  f->srcdir = g_strdup (g_getenv ("G_TEST_SRCDIR"));
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->srcdir == NULL)
    f->srcdir = g_path_get_dirname (argv0);

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);

  if (config == NULL)
    f->config = &defconfig;
  else
    f->config = config;

  if (f->config->type == DIRECT && g_stat ("/dev/input", &sb) != 0)
    {
      g_test_skip ("/dev/input not available");
      f->skipped = TRUE;
    }

  f->log = g_ptr_array_new_with_free_func (g_free);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->srcdir);
  g_free (f->builddir);

  g_clear_pointer (&f->log, g_ptr_array_unref);
  g_clear_pointer (&f->monitor_context, g_main_context_unref);
}

#define VENDOR_VALVE 0x28de
#define PRODUCT_VALVE_STEAM_CONTROLLER 0x1142

static SrtSimpleInputDevice *
load_json (Fixture *f,
           const char *filename)
{
  g_autoptr(SrtSimpleInputDevice) simple = NULL;
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(GError) error = NULL;
  JsonNode *node;
  JsonObject *object;
  JsonObject *added;
  g_autofree gchar *path = NULL;

  parser = json_parser_new ();

  path = g_build_filename (f->srcdir, "input-monitor-outputs",
                           filename, NULL);
  json_parser_load_from_file (parser, path, &error);
  g_assert_no_error (error);
  node = json_parser_get_root (parser);
  g_assert_nonnull (node);
  object = json_node_get_object (node);
  g_assert_nonnull (object);
  added = json_object_get_object_member (object, "added");
  g_assert_nonnull (added);
  simple = _srt_simple_input_device_new_from_json (added);
  g_assert_nonnull (simple);

  return g_steal_pointer (&simple);
}

static void
test_input_device_from_json_no_details (Fixture *f)
{
  g_autoptr(SrtSimpleInputDevice) simple = load_json (f, "no-details.json");
  SrtInputDevice *dev = SRT_INPUT_DEVICE (simple);
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
  } identity;
  unsigned long bits[LONGS_FOR_BITS (HIGHEST_EVENT_CODE)];
  g_autofree gchar *uevent = NULL;
  g_autoptr(GBytes) hid_report_descriptor = NULL;

  g_assert_cmphex (srt_input_device_get_interface_flags (dev),
                   ==, SRT_INPUT_DEVICE_INTERFACE_FLAGS_NONE);
  g_assert_cmphex (srt_input_device_get_type_flags (dev),
                   ==, SRT_INPUT_DEVICE_TYPE_FLAGS_NONE);
  g_assert_cmpstr (srt_input_device_get_dev_node (dev), ==, NULL);
  g_assert_cmpstr (srt_input_device_get_subsystem (dev), ==, NULL);
  g_assert_cmpstr (srt_input_device_get_sys_path (dev), ==, NULL);

  g_assert_false (srt_input_device_get_identity (dev,
                                                 NULL, NULL, NULL, NULL));
  g_assert_false (srt_input_device_get_identity (dev,
                                                 &identity.bus_type,
                                                 &identity.vendor_id,
                                                 &identity.product_id,
                                                 &identity.version));

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, 0,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    ==, 1);
  g_assert_cmphex (bits[0], ==, 0);
  g_assert_cmphex (bits[1], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_ABS,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmphex (bits[0], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_REL,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmphex (bits[0], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_KEY,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmphex (bits[0], ==, 0);
  g_assert_cmpuint (srt_input_device_get_input_properties (dev, bits,
                                                           G_N_ELEMENTS (bits)),
                    ==, 1);
  g_assert_cmphex (bits[0], ==, 0);

  uevent = srt_input_device_dup_uevent (dev);
  g_assert_cmpstr (uevent, ==, NULL);

  g_assert_cmpstr (srt_input_device_get_hid_sys_path (dev), ==, NULL);
  g_assert_false (srt_input_device_get_hid_identity (dev,
                                                     NULL, NULL, NULL,
                                                     NULL, NULL, NULL));
  uevent = srt_input_device_dup_hid_uevent (dev);
  g_assert_cmpstr (uevent, ==, NULL);

  g_assert_cmpstr (srt_input_device_get_input_sys_path (dev), ==, NULL);
  g_assert_false (srt_input_device_get_input_identity (dev,
                                                       NULL, NULL, NULL, NULL,
                                                       NULL, NULL, NULL));
  uevent = srt_input_device_dup_input_uevent (dev);
  g_assert_cmpstr (uevent, ==, NULL);

  g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (dev), ==, NULL);
  g_assert_false (srt_input_device_get_usb_device_identity (dev,
                                                            NULL, NULL, NULL,
                                                            NULL, NULL, NULL));
  uevent = srt_input_device_dup_usb_device_uevent (dev);
  g_assert_cmpstr (uevent, ==, NULL);

  hid_report_descriptor = srt_input_device_dup_hid_report_descriptor (dev);
  g_assert_null (hid_report_descriptor);
}

static void
test_input_device_from_json_odd (Fixture *f)
{
  g_autoptr(SrtSimpleInputDevice) simple = load_json (f, "odd.json");
  SrtInputDevice *dev = SRT_INPUT_DEVICE (simple);
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    const char *name;
    const char *phys;
    const char *uniq;
  } hid_identity;
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *name;
    const char *phys;
    const char *uniq;
  } input_identity;
  struct
  {
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *manufacturer;
    const char *product;
    const char *serial;
  } usb_identity;
  unsigned long bits[LONGS_FOR_BITS (HIGHEST_EVENT_CODE)];
  g_autoptr(GBytes) hid_report_descriptor = NULL;
  const guint8 *data;
  gsize len;

  g_assert_cmphex (srt_input_device_get_interface_flags (dev),
                   ==, SRT_INPUT_DEVICE_INTERFACE_FLAGS_RAW_HID);
  g_assert_cmphex (srt_input_device_get_type_flags (dev),
                   ==, SRT_INPUT_DEVICE_TYPE_FLAGS_NONE);
  g_assert_cmpstr (srt_input_device_get_dev_node (dev), ==, NULL);
  g_assert_cmpstr (srt_input_device_get_subsystem (dev), ==, NULL);
  g_assert_cmpstr (srt_input_device_get_sys_path (dev), ==, NULL);

  g_assert_false (srt_input_device_get_identity (dev,
                                                 NULL, NULL, NULL, NULL));

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, 0,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    ==, 1);
  g_assert_cmphex (bits[0], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_ABS,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
#if defined(__LP64__)
  g_assert_cmphex (bits[0], ==, 0x0807060504030201UL);
#elif defined(__i386__)
  g_assert_cmphex (bits[0], ==, 0x04030201UL);
  g_assert_cmphex (bits[1], ==, 0x08070605UL);
#endif

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_REL,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmphex (bits[0], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_KEY,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmphex (bits[0], ==, 0);
  g_assert_cmpuint (srt_input_device_get_input_properties (dev, bits,
                                                           G_N_ELEMENTS (bits)),
                    ==, 1);
#if defined(__LP64__)
  g_assert_cmphex (bits[0], ==, 0x2143658778563412UL);
#elif defined(__i386__)
  g_assert_cmphex (bits[0], ==, 0x78563412UL);
#endif
  g_assert_cmphex (bits[1], ==, 0);

  g_assert_true (srt_input_device_get_hid_identity (dev,
                                                    NULL, NULL, NULL,
                                                    NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_hid_identity (dev,
                                                    &hid_identity.bus_type,
                                                    &hid_identity.vendor_id,
                                                    &hid_identity.product_id,
                                                    &hid_identity.name,
                                                    &hid_identity.phys,
                                                    &hid_identity.uniq));
  g_assert_cmphex (hid_identity.bus_type, ==, 0xfff1);
  g_assert_cmphex (hid_identity.vendor_id, ==, 0xfff1);
  g_assert_cmphex (hid_identity.product_id, ==, 0xfff1);
  g_assert_cmpstr (hid_identity.name, ==, "Acme Weird Device");
  g_assert_cmpstr (hid_identity.phys, ==, NULL);
  g_assert_cmpstr (hid_identity.uniq, ==, "12345678");

  g_assert_true (srt_input_device_get_input_identity (dev,
                                                      NULL, NULL, NULL, NULL,
                                                      NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_input_identity (dev,
                                                      &input_identity.bus_type,
                                                      &input_identity.vendor_id,
                                                      &input_identity.product_id,
                                                      &input_identity.version,
                                                      &input_identity.name,
                                                      &input_identity.phys,
                                                      &input_identity.uniq));
  g_assert_cmphex (input_identity.bus_type, ==, 0xfff2);
  g_assert_cmphex (input_identity.vendor_id, ==, 0xfff2);
  g_assert_cmphex (input_identity.product_id, ==, 0xfff2);
  g_assert_cmphex (input_identity.version, ==, 0);
  g_assert_cmpstr (input_identity.name, ==, NULL);
  g_assert_cmpstr (input_identity.phys, ==, NULL);
  g_assert_cmpstr (input_identity.uniq, ==, "1234-5678");

  g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (dev), ==, "/...");
  g_assert_true (srt_input_device_get_usb_device_identity (dev,
                                                           NULL, NULL, NULL,
                                                           NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_usb_device_identity (dev,
                                                           &usb_identity.vendor_id,
                                                           &usb_identity.product_id,
                                                           &usb_identity.version,
                                                           &usb_identity.manufacturer,
                                                           &usb_identity.product,
                                                           &usb_identity.serial));
  g_assert_cmphex (usb_identity.vendor_id, ==, 0xfff3);
  g_assert_cmphex (usb_identity.product_id, ==, 0xfff3);
  g_assert_cmphex (usb_identity.version, ==, 0);
  g_assert_cmpstr (usb_identity.manufacturer, ==, NULL);
  g_assert_cmpstr (usb_identity.product, ==, NULL);
  g_assert_cmpstr (usb_identity.serial, ==, "12:34:56:78");

  hid_report_descriptor = srt_input_device_dup_hid_report_descriptor (dev);
  g_assert_nonnull (hid_report_descriptor);
  data = g_bytes_get_data (hid_report_descriptor, &len);
  g_assert_cmpuint (len, ==, 4);
  g_assert_cmpuint (data[0], ==, 0x12);
  g_assert_cmpuint (data[1], ==, 0x34);
  g_assert_cmpuint (data[2], ==, 0x56);
  g_assert_cmpuint (data[3], ==, 0x78);
}

static void
test_input_device_from_json_steam_controller (Fixture *f)
{
  g_autoptr(SrtSimpleInputDevice) simple = load_json (f, "steam-controller.json");
  SrtInputDevice *dev = SRT_INPUT_DEVICE (simple);
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
  } identity;
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    const char *name;
    const char *phys;
    const char *uniq;
  } hid_identity;
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *name;
    const char *phys;
    const char *uniq;
  } input_identity;
  struct
  {
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *manufacturer;
    const char *product;
    const char *serial;
  } usb_identity;
  unsigned long bits[LONGS_FOR_BITS (HIGHEST_EVENT_CODE)];
  g_autofree gchar *uevent = NULL;

  g_assert_cmphex (srt_input_device_get_interface_flags (dev),
                   ==, SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT);
  g_assert_cmphex (srt_input_device_get_type_flags (dev),
                   ==, (SRT_INPUT_DEVICE_TYPE_FLAGS_KEYBOARD
                        | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS
                        | SRT_INPUT_DEVICE_TYPE_FLAGS_MOUSE));
  g_assert_cmpstr (srt_input_device_get_dev_node (dev),
                   ==, "/dev/input/event20");
  g_assert_cmpstr (srt_input_device_get_subsystem (dev),
                   ==, "input");
  g_assert_cmpstr (srt_input_device_get_sys_path (dev),
                   ==, "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-1/1-1.1/1-1.1:1.0/0003:28DE:1142.00DD/input/input308/event20");

  g_assert_true (srt_input_device_get_identity (dev,
                                                NULL, NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_identity (dev,
                                                &identity.bus_type,
                                                &identity.vendor_id,
                                                &identity.product_id,
                                                &identity.version));
  /* Using magic numbers rather than a #define here so that it's easier
   * to validate against the JSON */
  g_assert_cmphex (identity.bus_type, ==, 0x0003);
  g_assert_cmphex (identity.vendor_id, ==, 0x28de);
  g_assert_cmphex (identity.product_id, ==, 0x1142);
  g_assert_cmphex (identity.version, ==, 0x0111);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, 0,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    ==, 1);
  g_assert_cmphex (bits[0], ==, 0x120017);
  g_assert_cmphex (bits[1], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_ABS,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmphex (bits[0], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_REL,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmphex (bits[0], ==, 0x0903);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (dev, EV_KEY,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
#if defined(__LP64__)
  g_assert_cmphex (bits[0], ==, 0xfffffffffffffffeUL);
  g_assert_cmphex (bits[1], ==, 0xe080ffdf01cfffffUL);
  g_assert_cmphex (bits[2], ==, 0);
  g_assert_cmphex (bits[3], ==, 0);
  g_assert_cmphex (bits[4], ==, 0x1f0000);
  g_assert_cmphex (bits[5], ==, 0);
#elif defined(__i386__)
  g_assert_cmphex (bits[0], ==, 0xfffffffeUL);
  g_assert_cmphex (bits[1], ==, 0xffffffffUL);
  g_assert_cmphex (bits[2], ==, 0x01cfffffUL);
  g_assert_cmphex (bits[3], ==, 0xe080ffdfUL);
  g_assert_cmphex (bits[4], ==, 0);
  g_assert_cmphex (bits[5], ==, 0);
  g_assert_cmphex (bits[6], ==, 0);
  g_assert_cmphex (bits[7], ==, 0);
  g_assert_cmphex (bits[8], ==, 0x1f0000);
  g_assert_cmphex (bits[9], ==, 0);
  g_assert_cmphex (bits[10], ==, 0);
  g_assert_cmphex (bits[11], ==, 0);
#endif
  g_assert_cmpuint (srt_input_device_get_input_properties (dev, bits,
                                                           G_N_ELEMENTS (bits)),
                    ==, 1);
  g_assert_cmphex (bits[0], ==, 0);

  uevent = srt_input_device_dup_uevent (dev);
  g_assert_cmpstr (uevent, ==,
                   "MAJOR=13\n"
                   "MINOR=84\n"
                   "DEVNAME=input/event20\n");
  g_clear_pointer (&uevent, g_free);

  g_assert_cmpstr (srt_input_device_get_hid_sys_path (dev),
                   ==, "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-1/1-1.1/1-1.1:1.0/0003:28DE:1142.00DD");
  g_assert_true (srt_input_device_get_hid_identity (dev,
                                                    NULL, NULL, NULL,
                                                    NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_hid_identity (dev,
                                                    &hid_identity.bus_type,
                                                    &hid_identity.vendor_id,
                                                    &hid_identity.product_id,
                                                    &hid_identity.name,
                                                    &hid_identity.phys,
                                                    &hid_identity.uniq));
  g_assert_cmphex (hid_identity.bus_type, ==, 0x0003);
  g_assert_cmphex (hid_identity.vendor_id, ==, 0x28de);
  g_assert_cmphex (hid_identity.product_id, ==, 0x1142);
  g_assert_cmpstr (hid_identity.name, ==, "Valve Software Steam Controller");
  g_assert_cmpstr (hid_identity.phys, ==, "usb-0000:00:14.0-1.1/input0");
  g_assert_cmpstr (hid_identity.uniq, ==, "");
  uevent = srt_input_device_dup_hid_uevent (dev);
  g_assert_cmpstr (uevent, ==,
                   "DRIVER=hid-steam\n"
                   "HID_ID=0003:000028DE:00001142\n"
                   "HID_NAME=Valve Software Steam Controller\n"
                   "HID_PHYS=usb-0000:00:14.0-1.1/input0\n"
                   "HID_UNIQ=\n"
                   "MODALIAS=hid:b0003g0001v000028DEp00001142\n");
  g_clear_pointer (&uevent, g_free);

  g_assert_cmpstr (srt_input_device_get_input_sys_path (dev),
                   ==, "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-1/1-1.1/1-1.1:1.0/0003:28DE:1142.00DD/input/input308");
  g_assert_true (srt_input_device_get_input_identity (dev,
                                                      NULL, NULL, NULL, NULL,
                                                      NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_input_identity (dev,
                                                      &input_identity.bus_type,
                                                      &input_identity.vendor_id,
                                                      &input_identity.product_id,
                                                      &input_identity.version,
                                                      &input_identity.name,
                                                      &input_identity.phys,
                                                      &input_identity.uniq));
  g_assert_cmphex (input_identity.bus_type, ==, 0x0003);
  g_assert_cmphex (input_identity.vendor_id, ==, 0x28de);
  g_assert_cmphex (input_identity.product_id, ==, 0x1142);
  g_assert_cmphex (input_identity.version, ==, 0x0111);
  g_assert_cmpstr (input_identity.name, ==, "Valve Software Steam Controller");
  g_assert_cmpstr (input_identity.phys, ==, "usb-0000:00:14.0-1.1/input0");
  g_assert_cmpstr (input_identity.uniq, ==, NULL);
  uevent = srt_input_device_dup_input_uevent (dev);
  g_assert_cmpstr (uevent, ==,
                   "PRODUCT=3/28de/1142/111\n"
                   "NAME=\"Valve Software Steam Controller\"\n"
                   "PHYS=\"usb-0000:00:14.0-1.1/input0\"\n"
                   "UNIQ=\"\"\n"
                   "PROP=0\n"
                   "EV=120017\n"
                   "KEY=1f0000 0 0 e080ffdf01cfffff fffffffffffffffe\n"
                   "REL=903\n"
                   "MSC=10\n"
                   "LED=1f\n"
                   "MODALIAS=input:b0003v28DEp1142e0111-e0,1,2,4,11,14,k77,7D,7E,7F,110,111,112,113,114,r0,1,8,B,am4,l0,1,2,3,4,sfw\n");
  g_clear_pointer (&uevent, g_free);

  g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (dev),
                   ==, "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-1/1-1.1");
  g_assert_true (srt_input_device_get_usb_device_identity (dev,
                                                           NULL, NULL, NULL,
                                                           NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_usb_device_identity (dev,
                                                           &usb_identity.vendor_id,
                                                           &usb_identity.product_id,
                                                           &usb_identity.version,
                                                           &usb_identity.manufacturer,
                                                           &usb_identity.product,
                                                           &usb_identity.serial));
  g_assert_cmphex (usb_identity.vendor_id, ==, 0x28de);
  g_assert_cmphex (usb_identity.product_id, ==, 0x1142);
  g_assert_cmphex (usb_identity.version, ==, 0x0001);
  g_assert_cmpstr (usb_identity.manufacturer, ==, "Valve Software");
  g_assert_cmpstr (usb_identity.product, ==, "Steam Controller");
  g_assert_cmpstr (usb_identity.serial, ==, NULL);
  uevent = srt_input_device_dup_usb_device_uevent (dev);
  g_assert_cmpstr (uevent, ==,
                   "MAJOR=189\n"
                   "MINOR=66\n"
                   "DEVNAME=bus/usb/001/067\n"
                   "DEVTYPE=usb_device\n"
                   "DRIVER=usb\n"
                   "PRODUCT=28de/1142/1\n"
                   "TYPE=0/0/0\n"
                   "BUSNUM=001\n"
                   "DEVNUM=067\n");
  g_clear_pointer (&uevent, g_free);
}

static void
test_input_device_from_json (Fixture *f,
                             gconstpointer context)
{
  test_input_device_from_json_no_details (f);
  test_input_device_from_json_odd (f);
  test_input_device_from_json_steam_controller (f);
}

typedef struct
{
  const char *name;
  const char *eviocgname;
  const char *usb_vendor_name;
  const char *usb_product_name;
  guint16 bus_type;
  guint16 vendor_id;
  guint16 product_id;
  guint16 version;
  guint16 usb_device_version;
  guint8 ev[(EV_MAX + 1) / 8];
  guint8 keys[(KEY_MAX + 1) / 8];
  guint8 abs[(ABS_MAX + 1) / 8];
  guint8 rel[(REL_MAX + 1) / 8];
  guint8 ff[(FF_MAX + 1) / 8];
  guint8 props[(INPUT_PROP_MAX + 1) / 8];
  SrtInputDeviceTypeFlags expected;
  const char *todo;
  size_t hid_report_descriptor_length;
  const unsigned char *hid_report_descriptor;
} GuessTest;

/*
 * Test-cases for guessing a device type from its capabilities.
 *
 * The bytes in ev, etc. are in little-endian byte order, the same as
 * the JSON output from input-monitor. Trailing zeroes can be omitted.
 */
#define ZEROx4 0, 0, 0, 0
#define ZEROx8 ZEROx4, ZEROx4
#define FFx4 0xff, 0xff, 0xff, 0xff
#define FFx8 FFx4, FFx4

static unsigned char xbox_one_elite_2_hid_report_descriptor[] =
{
    /* Generic Desktop / Game Pad, Generic Desktop / Keyboard */
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01,
    0x09, 0x01, 0xa1, 0x00, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, 0x95,
    0x02, 0x75, 0x10, 0x81, 0x02, 0xc0, 0x09, 0x01,
    0xa1, 0x00, 0x09, 0x32, 0x09, 0x35, 0x15, 0x00,
    0x27, 0xff, 0xff, 0x00, 0x00, 0x95, 0x02, 0x75,
    0x10, 0x81, 0x02, 0xc0, 0x05, 0x02, 0x09, 0xc5,
    0x15, 0x00, 0x26, 0xff, 0x03, 0x95, 0x01, 0x75,
    0x0a, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00, 0x75,
    0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x02, 0x09,
    0xc4, 0x15, 0x00, 0x26, 0xff, 0x03, 0x95, 0x01,
    0x75, 0x0a, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00,
    0x75, 0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x01,
    0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x35, 0x00,
    0x46, 0x3b, 0x01, 0x66, 0x14, 0x00, 0x75, 0x04,
    0x95, 0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01,
    0x15, 0x00, 0x25, 0x00, 0x35, 0x00, 0x45, 0x00,
    0x65, 0x00, 0x81, 0x03, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x0f, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x0f, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00,
    0x75, 0x01, 0x95, 0x01, 0x81, 0x03, 0x05, 0x0c,
    0x0a, 0xb2, 0x00, 0x15, 0x00, 0x25, 0x01, 0x95,
    0x01, 0x75, 0x01, 0x81, 0x02, 0x15, 0x00, 0x25,
    0x00, 0x75, 0x07, 0x95, 0x01, 0x81, 0x03, 0x05,
    0x0c, 0x09, 0x01, 0xa1, 0x01, 0x0a, 0x85, 0x00,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x95, 0x01, 0x75,
    0x08, 0x81, 0x02, 0x0a, 0x99, 0x00, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x95, 0x01, 0x75, 0x04, 0x81,
    0x02, 0x15, 0x00, 0x25, 0x00, 0x95, 0x01, 0x75,
    0x04, 0x81, 0x03, 0x0a, 0x81, 0x00, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x95, 0x01, 0x75, 0x04, 0x81,
    0x02, 0x15, 0x00, 0x25, 0x00, 0x95, 0x01, 0x75,
    0x04, 0x81, 0x03, 0xc0, 0x05, 0x0f, 0x09, 0x21,
    0x85, 0x03, 0xa1, 0x02, 0x09, 0x97, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x04, 0x95, 0x01, 0x91, 0x02,
    0x15, 0x00, 0x25, 0x00, 0x75, 0x04, 0x95, 0x01,
    0x91, 0x03, 0x09, 0x70, 0x15, 0x00, 0x25, 0x64,
    0x75, 0x08, 0x95, 0x04, 0x91, 0x02, 0x09, 0x50,
    0x66, 0x01, 0x10, 0x55, 0x0e, 0x15, 0x00, 0x26,
    0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
    0x09, 0xa7, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75,
    0x08, 0x95, 0x01, 0x91, 0x02, 0x65, 0x00, 0x55,
    0x00, 0x09, 0x7c, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, 0x01, 0x91, 0x02, 0xc0, 0x05,
    0x0c, 0x09, 0x01, 0x85, 0x0c, 0xa1, 0x01, 0x0a,
    0x9e, 0x00, 0x15, 0x00, 0x26, 0xff, 0x00, 0x95,
    0x01, 0x75, 0x08, 0x81, 0x02, 0x0a, 0xa1, 0x00,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x95, 0x01, 0x75,
    0x08, 0x81, 0x02, 0x0a, 0xa2, 0x00, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x95, 0x01, 0x75, 0x08, 0x81,
    0x02, 0x0a, 0xa3, 0x00, 0x15, 0x00, 0x26, 0xff,
    0x00, 0x95, 0x01, 0x75, 0x08, 0x81, 0x02, 0xc0,
    0xc0, 0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85,
    0x05, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15,
    0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81,
    0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95,
    0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05,
    0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0,
};
G_STATIC_ASSERT (sizeof (xbox_one_elite_2_hid_report_descriptor) == 0720);

static unsigned char ps3_hid_report_descriptor[] =
{
    /* Generic Desktop / Joystick */
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xa1, 0x02,
    0x85, 0x01, 0x75, 0x08, 0x95, 0x01, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x81, 0x03, 0x75, 0x01, 0x95,
    0x13, 0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45,
    0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x13, 0x81,
    0x02, 0x75, 0x01, 0x95, 0x0d, 0x06, 0x00, 0xff,
    0x81, 0x03, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05,
    0x01, 0x09, 0x01, 0xa1, 0x00, 0x75, 0x08, 0x95,
    0x04, 0x35, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30,
    0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
    0xc0, 0x05, 0x01, 0x75, 0x08, 0x95, 0x27, 0x09,
    0x01, 0x81, 0x02, 0x75, 0x08, 0x95, 0x30, 0x09,
    0x01, 0x91, 0x02, 0x75, 0x08, 0x95, 0x30, 0x09,
    0x01, 0xb1, 0x02, 0xc0, 0xa1, 0x02, 0x85, 0x02,
    0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xb1, 0x02,
    0xc0, 0xa1, 0x02, 0x85, 0xee, 0x75, 0x08, 0x95,
    0x30, 0x09, 0x01, 0xb1, 0x02, 0xc0, 0xa1, 0x02,
    0x85, 0xef, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01,
    0xb1, 0x02, 0xc0, 0xc0, 0x00,
};
G_STATIC_ASSERT (sizeof (ps3_hid_report_descriptor) == 149);

/* Same for Steam Deck LCD (jupiter) and OLED (galileo) */
static unsigned char steam_deck_mouse_hid_report_descriptor[] =
{
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01,
    0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x02,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02,
    0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81,
    0x25, 0x7f, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
    0x95, 0x01, 0x09, 0x38, 0x81, 0x06, 0x05, 0x0c,
    0x0a, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06, 0xc0,
    0xc0,
};
G_STATIC_ASSERT (sizeof (steam_deck_mouse_hid_report_descriptor) == 65);

/* Same for Steam Deck LCD (jupiter) and OLED (galileo) */
static unsigned char steam_deck_kb_hid_report_descriptor[] =
{
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07,
    0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x81, 0x01,
    0x19, 0x00, 0x29, 0x65, 0x15, 0x00, 0x25, 0x65,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x00, 0xc0,
};
G_STATIC_ASSERT (sizeof (steam_deck_kb_hid_report_descriptor) == 39);

static unsigned char steam_deck_lcd_js_hid_report_descriptor[] =
{
    0x06, 0xff, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x15,
    0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40,
    0x09, 0x01, 0x81, 0x02, 0x09, 0x01, 0xb1, 0x02,
    0xc0,
};
G_STATIC_ASSERT (sizeof (steam_deck_lcd_js_hid_report_descriptor) == 25);

static unsigned char steam_deck_oled_js_hid_report_descriptor[] =
{
    0x06, 0xff, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x09,
    0x02, 0x09, 0x03, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, 0x40, 0x81, 0x02, 0x09, 0x06,
    0x09, 0x07, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75,
    0x08, 0x95, 0x40, 0xb1, 0x02, 0xc0,
};
G_STATIC_ASSERT (sizeof (steam_deck_oled_js_hid_report_descriptor) == 38);

static unsigned char vrs_pedals_hid_report_descriptor[] =
{
    /* Generic Desktop / Joystick */
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x05, 0x01,
    0xa1, 0x02, 0x85, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x32, 0x15, 0x00, 0x27, 0xff, 0xff, 0x00,
    0x00, 0x35, 0x00, 0x47, 0xff, 0xff, 0x00, 0x00,
    0x75, 0x10, 0x95, 0x03, 0x81, 0x02, 0x06, 0x00,
    0xff, 0x09, 0x01, 0x95, 0x39, 0x75, 0x08, 0x26,
    0xff, 0x00, 0x15, 0x00, 0x81, 0x02, 0xc0, 0x06,
    0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x64,
    0x95, 0x3f, 0x75, 0x08, 0x26, 0xff, 0x00, 0x15,
    0x00, 0x09, 0x01, 0x91, 0x02, 0x85, 0x65, 0x95,
    0x3f, 0x75, 0x08, 0x26, 0xff, 0x00, 0x15, 0x00,
    0x09, 0x01, 0x81, 0x02, 0xc0, 0xc0,
};
G_STATIC_ASSERT (sizeof (vrs_pedals_hid_report_descriptor) == 0136);

static unsigned char thinkpad_usb_keyboard_hid_report_descriptor[] =
{
    /* Generic Desktop / Keyboard */
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07,
    0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x08, 0x75, 0x01, 0x81, 0x02, 0x95, 0x08,
    0x75, 0x01, 0x81, 0x01, 0x05, 0x08, 0x19, 0x01,
    0x29, 0x03, 0x95, 0x03, 0x75, 0x01, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x91, 0x01, 0x05, 0x07,
    0x19, 0x00, 0x2a, 0xff, 0x00, 0x15, 0x00, 0x26,
    0xff, 0x00, 0x95, 0x06, 0x75, 0x08, 0x81, 0x00,
    0xc0,
};
G_STATIC_ASSERT (sizeof (thinkpad_usb_keyboard_hid_report_descriptor) == 65);

static unsigned char thinkpad_usb_trackpoint_hid_report_descriptor[] =
{
    /* Generic Desktop / Mouse, Generic Desktop / System Control,
     * Consumer Devices / Consumer Control */
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x01,
    0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x81, 0x25, 0x7f, 0x95, 0x02, 0x75, 0x08,
    0x81, 0x06, 0xc0, 0xc0, 0x05, 0x01, 0x09, 0x80,
    0xa1, 0x01, 0x85, 0x02, 0x05, 0x01, 0x15, 0x00,
    0x25, 0x01, 0x95, 0x08, 0x75, 0x01, 0x19, 0x81,
    0x29, 0x88, 0x81, 0x02, 0xc0, 0x05, 0x0c, 0x09,
    0x01, 0xa1, 0x01, 0x85, 0x03, 0x95, 0x08, 0x75,
    0x01, 0x15, 0x00, 0x25, 0x01, 0x09, 0xe9, 0x09,
    0xea, 0x09, 0xe2, 0x09, 0xb7, 0x09, 0xcd, 0x09,
    0xb5, 0x09, 0xb6, 0x0a, 0x94, 0x01, 0x81, 0x02,
    0x09, 0x03, 0xa1, 0x02, 0x05, 0x09, 0x19, 0x10,
    0x29, 0x17, 0x81, 0x02, 0x05, 0x09, 0x19, 0x18,
    0x29, 0x1f, 0x81, 0x02, 0xc0, 0x05, 0x08, 0x95,
    0x02, 0x09, 0x09, 0x09, 0x21, 0x91, 0x02, 0x95,
    0x01, 0x75, 0x06, 0x91, 0x03, 0xc0, 0x06, 0x01,
    0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x04, 0x95,
    0x01, 0x75, 0x08, 0x15, 0x00, 0x26, 0xff, 0x00,
    0x09, 0x20, 0xb1, 0x03, 0x09, 0x21, 0xb1, 0x03,
    0x09, 0x22, 0xb1, 0x03, 0x09, 0x23, 0xb1, 0x03,
    0xc0,
};
G_STATIC_ASSERT (sizeof (thinkpad_usb_trackpoint_hid_report_descriptor) == 185);

static unsigned char heusinkveld_pedals_hid_report_descriptor[] =
{
    /* Generic Desktop / Joystick */
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x09, 0x33,
    0x09, 0x34, 0x09, 0x35, 0x15, 0x00, 0x26, 0xff,
    0x0f, 0x85, 0x01, 0x75, 0x10, 0x95, 0x03, 0x81,
    0x02, 0x09, 0x00, 0x75, 0x10, 0x95, 0x06, 0x82,
    0x01, 0x01, 0x85, 0x02, 0x75, 0x10, 0x95, 0x03,
    0x09, 0x00, 0x09, 0x00, 0xb1, 0x02, 0x85, 0x03,
    0x75, 0x08, 0x95, 0x03, 0x09, 0x00, 0x82, 0x01,
    0x01, 0xc0,
};
G_STATIC_ASSERT (sizeof (heusinkveld_pedals_hid_report_descriptor) == 072);

static unsigned char fanatec_handbrake_hid_report_descriptor[] =
{
    /* Generic Desktop / Joystick */
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x95, 0x01, 0x75, 0x08, 0x09,
    0x30, 0x81, 0x02, 0x06, 0x00, 0xff, 0x09, 0x01,
    0x95, 0x03, 0x81, 0x02, 0x06, 0x00, 0xff, 0x09,
    0x01, 0x95, 0x02, 0x91, 0x02, 0xc0,
};
G_STATIC_ASSERT (sizeof (fanatec_handbrake_hid_report_descriptor) == 046);

static unsigned char xpadneo09_xb1s_hid_report_descriptor[] =
{
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01,
    0x09, 0x01, 0xa1, 0x00, 0x09, 0x30, 0x09, 0x31,
    0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, 0x95,
    0x02, 0x75, 0x10, 0x81, 0x02, 0xc0, 0x09, 0x01,
    0xa1, 0x00, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00,
    0x27, 0xff, 0xff, 0x00, 0x00, 0x95, 0x02, 0x75,
    0x10, 0x81, 0x02, 0xc0, 0x05, 0x01, 0x09, 0x32,
    0x15, 0x00, 0x26, 0xff, 0x03, 0x95, 0x01, 0x75,
    0x0a, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00, 0x75,
    0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x01, 0x09,
    0x35, 0x15, 0x00, 0x26, 0xff, 0x03, 0x95, 0x01,
    0x75, 0x0a, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00,
    0x75, 0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x01,
    0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x35, 0x00,
    0x46, 0x3b, 0x01, 0x66, 0x14, 0x00, 0x75, 0x04,
    0x95, 0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01,
    0x15, 0x00, 0x25, 0x00, 0x35, 0x00, 0x45, 0x00,
    0x65, 0x00, 0x81, 0x03, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x0c, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x0c, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00,
    0x75, 0x01, 0x95, 0x04, 0x81, 0x03, 0x05, 0x0c,
    0x0a, 0xb2, 0x00, 0x15, 0x00, 0x25, 0x01, 0x95,
    0x01, 0x75, 0x01, 0x81, 0x02, 0x15, 0x00, 0x25,
    0x00, 0x75, 0x07, 0x95, 0x01, 0x81, 0x03, 0x05,
    0x0f, 0x09, 0x21, 0x85, 0x03, 0xa1, 0x02, 0x09,
    0x97, 0x15, 0x00, 0x25, 0x01, 0x75, 0x04, 0x95,
    0x01, 0x91, 0x02, 0x15, 0x00, 0x25, 0x00, 0x75,
    0x04, 0x95, 0x01, 0x91, 0x03, 0x09, 0x70, 0x15,
    0x00, 0x25, 0x64, 0x75, 0x08, 0x95, 0x04, 0x91,
    0x02, 0x09, 0x50, 0x66, 0x01, 0x10, 0x55, 0x0e,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
    0x01, 0x91, 0x02, 0x09, 0xa7, 0x15, 0x00, 0x26,
    0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
    0x65, 0x00, 0x55, 0x00, 0x09, 0x7c, 0x15, 0x00,
    0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91,
    0x02, 0xc0, 0xc0,
};
G_STATIC_ASSERT (sizeof (xpadneo09_xb1s_hid_report_descriptor) == 283);

static const GuessTest guess_tests[] =
{
    {
      .name = "Xbox 360 wired USB controller",
      .eviocgname = "Microsoft X-Box 360 pad",
      .usb_vendor_name = "©Microsoft Corporation",
      .usb_product_name = "Controller",
      /* 8BitDo N30 Pro 2 v0114 via USB-C (with the xpad driver) is
       * reported as 0003:045e:028e v0114, and is functionally equivalent */
      .bus_type = 0x0003,
      .vendor_id = 0x045e,
      .product_id = 0x028e,
      .version = 0x0114,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, FF */
      .ev = { 0x0b, 0x00, 0x20 },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    {
      .name = "X-Box One Elite",
      .bus_type = 0x0003,
      .vendor_id = 0x045e,
      .product_id = 0x02e3,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    { /* Reference: https://github.com/libsdl-org/SDL/issues/7814 */
      .name = "X-Box One Elite 2 via USB",
      /* The same physical device via Bluetooth, 0005:045e:0b22 v0517,
       * is reported differently (below). */
      /* Version 0407 is functionally equivalent. */
      .bus_type = 0x0003,
      .vendor_id = 0x045e,
      .product_id = 0x0b00,
      .version = 0x0511,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, FF */
      .ev = { 0x0b, 0x00, 0x20 },
      /* XY (left stick), RX/RY (right stick), Z/RZ (triggers), HAT0 (dpad) */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* BTN_TRIGGER_HAPPY5 up to BTN_TRIGGER_HAPPY8 inclusive are the
           * back buttons (paddles) */
          /* 0x2c0 */ 0xf0,
      },
    },
    { /* Reference: https://github.com/libsdl-org/SDL/issues/7814 */
      .name = "X-Box One Elite 2 via Bluetooth",
      /* The same physical device via USB, 0003:045e:0b00 v0511,
       * is reported differently (above). */
      .bus_type = 0x0003,
      .vendor_id = 0x045e,
      .product_id = 0x0b22,
      .version = 0x0517,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      /* SYN, KEY, ABS, FF */
      .ev = { 0x0b, 0x00, 0x20 },
      /* Android-style mapping:
       * XY (left stick), Z/RZ (right stick), GAS/BRAKE (triggers), HAT0 (dpad) */
      .abs = { 0x27, 0x06, 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx8,
          /* KEY_RECORD is advertised but isn't generated in practice */
          /* 0x80 */ ZEROx4, 0x80, 0x00, 0x00, 0x00,
          /* KEY_UNKNOWN (240) is reported for the profile selector and all
           * four back buttons (paddles) */
          /* 0xc0 */ ZEROx4, 0x00, 0x00, 0x01, 0x00,
          /* ABXY, TL, TR, TL2, TR2, SELECT, START, MODE, THUMBL,
           * THUMBR have their obvious meanings; C and Z are also
           * advertised, but are not generated in practice. */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x7f,
      },
      .hid_report_descriptor_length = sizeof (xbox_one_elite_2_hid_report_descriptor),
      .hid_report_descriptor = &xbox_one_elite_2_hid_report_descriptor[0],
    },
    {
      .name = "X-Box One S via Bluetooth",
      .bus_type = 0x0005,
      .vendor_id = 0x045e,
      .product_id = 0x02e0,
      .version = 0x1130,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    {
      .name = "X-Box One S wired",
      .bus_type = 0x0003,
      .vendor_id = 0x045e,
      .product_id = 0x02ea,
      .version = 0x0301,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    {
      .name = "X-Box One S via xpadneo 0.9.x",
      /* Reference: https://github.com/libsdl-org/SDL/issues/7823 */
      .eviocgname = "Xbox Wireless Controller",
      .bus_type = 0x0005,
      .vendor_id = 0x045e,
      .product_id = 0x028e,
      .version = 0x1130,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC, FF */
      .ev = { 0x1b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y
       * plus MISC as a deprecated axis reporting (rz - z) */
      .abs = { 0x3f, 0x00, 0x03, 0x00, 0x00, 0x01 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* BTN_TRIGGER_HAPPY33 up to BTN_TRIGGER_HAPPY36 inclusive:
           * used to represent the current profile */
          /* 0x2c0 */ ZEROx4, 0xf0,
      },
      .hid_report_descriptor_length = sizeof (xpadneo09_xb1s_hid_report_descriptor),
      .hid_report_descriptor = &xpadneo09_xb1s_hid_report_descriptor[0],
    },
    {
      .name = "DualSense (PS5) - gamepad",
      .bus_type = 0x0003,
      .vendor_id = 0x054c,
      .product_id = 0x0ce6,
      .version = 0x111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, select, start, mode, thumbl,
           * thumbr; note that C and Z don't physically exist */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x7f,
      },
    },
    {
      .name = "DualSense (PS5) v8111 - gamepad",
      .eviocgname = "Sony Interactive Entertainment Wireless Controller",
      .usb_vendor_name = "Sony Interactive Entertainment",
      .usb_product_name = "Wireless Controller",
      /* Same physical device via Bluetooth is 0005:054c:0ce6 v8100
       * and EVIOCGNAME is just "Wireless Controller", otherwise equivalent */
      .bus_type = 0x0003,
      .vendor_id = 0x054c,
      .product_id = 0x0ce6,
      .version = 0x8111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7f,
      },
    },
    {
      .name = "DualShock 4 - gamepad",
      /* EVIOCGNAME is just "Wireless Controller" when seen via Bluetooth */
      .eviocgname = "Sony Interactive Entertainment Wireless Controller",
      .usb_vendor_name = "Sony Interactive Entertainment",
      .usb_product_name = "Wireless Controller",
      /* Same physical device via Bluetooth is 0005:054c:09cc v8100,
       * but otherwise equivalent */
      .bus_type = 0x0003,
      .vendor_id = 0x054c,
      .product_id = 0x09cc,
      .version = 0x8111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC, FF */
      /* Some versions only have 0x0b, SYN, KEY, ABS, like the
       * Bluetooth example below */
      .ev = { 0x1b, 0x00, 0x20 },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7f,
      },
    },
    {
      .name = "DualShock 4 - gamepad via Bluetooth (unknown version)",
      .bus_type = 0x0005,
      .vendor_id = 0x054c,
      .product_id = 0x09cc,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7f,
      },
    },
    {
      .name = "DualShock 4 - touchpad",
      /* EVIOCGNAME is just "Wireless Controller Touchpad" when seen via Bluetooth */
      .eviocgname = "Sony Interactive Entertainment Wireless Controller Touchpad",
      .usb_vendor_name = "Sony Interactive Entertainment",
      .usb_product_name = "Wireless Controller",
      /* Same physical device via Bluetooth is 0005:054c:09cc v8100 and is
       * functionally equivalent. */
      /* DualSense (PS5), 0003:054c:0ce6 v8111, is functionally equivalent.
       * Same physical device via Bluetooth is 0005:054c:0ce6 v8100 and also
       * functionally equivalent. */
      .bus_type = 0x0003,
      .vendor_id = 0x054c,
      .product_id = 0x09cc,
      .version = 0x8111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHPAD,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, multitouch */
      .abs = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0x60, 0x02 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* Left mouse button */
          /* 0x100 */ 0x00, 0x00, 0x01, 0x00, ZEROx4,
          /* BTN_TOOL_FINGER and some multitouch stuff */
          /* 0x140 */ 0x20, 0x24, 0x00, 0x00
      },
      /* POINTER, BUTTONPAD */
      .props = { 0x05 },
    },
    {
      .name = "DualShock 4 - accelerometer",
      /* EVIOCGNAME is just "Wireless Controller Motion Sensors" when seen via Bluetooth */
      .eviocgname = "Sony Interactive Entertainment Wireless Controller Motion Sensors",
      .usb_vendor_name = "Sony Interactive Entertainment",
      .usb_product_name = "Wireless Controller",
      /* Same physical device via Bluetooth is 0005:054c:09cc v8100 and is
       * functionally equivalent. */
      /* DualSense (PS5), 0003:054c:0ce6 v8111, is functionally equivalent.
       * Same physical device via Bluetooth is 0005:054c:0ce6 v8100 and also
       * functionally equivalent. */
      .bus_type = 0x0003,
      .vendor_id = 0x054c,
      .product_id = 0x09cc,
      .version = 0x8111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER,
      /* SYN, ABS, MSC */
      .ev = { 0x19 },
      /* X, Y, Z, RX, RY, RZ */
      .abs = { 0x3f },
      /* ACCELEROMETER */
      .props = { 0x40 },
    },
    {
      .name = "DualShock 4 via USB dongle",
      .bus_type = 0x0003,
      .vendor_id = 0x054c,
      .product_id = 0x0ba0,
      .version = 0x8111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, ABS, KEY */
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7f,
      },
    },
    {
      .name = "DualShock 3 - gamepad",
      .eviocgname = "Sony PLAYSTATION(R)3 Controller",
      .usb_vendor_name = "Sony",
      .usb_product_name = "PLAYSTATION(R)3 Controller",
      .bus_type = 0x0003,
      .vendor_id = 0x054c,
      .product_id = 0x0268,
      .version = 0x8111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC, FF */
      .ev = { 0x1b, 0x00, 0x20 },
      /* X, Y, Z, RX, RY, RZ */
      .abs = { 0x3f },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7f,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* Digital dpad */
          /* 0x200 */ ZEROx4, 0x0f, 0x00, 0x00, 0x00,
      },
      .hid_report_descriptor_length = sizeof (ps3_hid_report_descriptor),
      .hid_report_descriptor = &ps3_hid_report_descriptor[0],
    },
    {
      .name = "DualShock 3 - accelerometer",
      .eviocgname = "Sony PLAYSTATION(R)3 Controller Motion Sensors",
      .usb_vendor_name = "Sony",
      .usb_product_name = "PLAYSTATION(R)3 Controller",
      .bus_type = 0x0003,
      .vendor_id = 0x054c,
      .product_id = 0x0268,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER,
      /* SYN, ABS */
      .ev = { 0x09 },
      /* X, Y, Z */
      .abs = { 0x07 },
      /* ACCELEROMETER */
      .props = { 0x40 },
      .hid_report_descriptor_length = sizeof (ps3_hid_report_descriptor),
      .hid_report_descriptor = &ps3_hid_report_descriptor[0],
    },
    {
      .name = "Steam Controller - gamepad",
      .bus_type = 0x0003,
      .vendor_id = 0x28de,
      .product_id = 0x1142,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, RX, RY, HAT0X, HAT0Y, HAT2X, HAT2Y */
      .abs = { 0x1b, 0x00, 0x33 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL, THUMBR, joystick THUMB, joystick THUMB2  */
          /* 0x100 */ ZEROx4, 0x06, 0x00, 0xdb, 0x7f,
          /* GEAR_DOWN, GEAR_UP */
          /* 0x140 */ 0x00, 0x00, 0x03, 0x00, ZEROx4,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* Digital dpad */
          /* 0x200 */ ZEROx4, 0x0f, 0x00, 0x00, 0x00,
      },
    },
    {
      /* Present to support lizard mode, even if no Steam Controller
       * is connected */
      .name = "Steam Controller - dongle",
      .bus_type = 0x0003,
      .vendor_id = 0x28de,
      .product_id = 0x1142,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_KEYBOARD
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_MOUSE),
      /* SYN, KEY, REL, MSC, LED, REP */
      .ev = { 0x17, 0x00, 0x12 },
      /* X, Y, mouse wheel, high-res mouse wheel */
      .rel = { 0x03, 0x09 },
      .keys = {
          /* 0x00 */ 0xfe, 0xff, 0xff, 0xff, FFx4,
          /* 0x40 */ 0xff, 0xff, 0xcf, 0x01, 0xdf, 0xff, 0x80, 0xe0,
          /* 0x80 */ ZEROx8,
          /* 0xc0 */ ZEROx8,
          /* 0x100 */ 0x00, 0x00, 0x1f, 0x00, ZEROx4,
      },
    },
    {
      .name = "Steam Deck - mouse",
      /* This is the LCD model (jupiter).
       * Steam Deck OLED (galileo, possibly pre-production) has
       * .eviocgname = "Valve Software Steam Controller"
       * .version = 0x0110
       * .usb_device_version = 0x0300
       * but is otherwise equivalent.
       */
      .eviocgname = "Valve Software Steam Deck Controller",
      .usb_vendor_name = "Valve Software",
      .usb_product_name = "Steam Deck Controller",
      .bus_type = 0x0003,
      .vendor_id = 0x28de,
      .product_id = 0x1205,
      .version = 0x011,
      .usb_device_version = 0x0200,
      /* SYN, KEY, REL, MSC */
      .ev = { 0x17 },
      /* X, Y, mouse wheel v/h, high-res mouse wheel v/h */
      .rel = { 0x43, 0x19 },
      .keys = {
        /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
        /* left/right mouse button */
        /* 0x100 */ 0x00, 0x00, 0x03, 0x00, ZEROx4,
      },
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_MOUSE,
      .hid_report_descriptor_length = sizeof (steam_deck_mouse_hid_report_descriptor),
      .hid_report_descriptor = &steam_deck_mouse_hid_report_descriptor[0],
    },
    {
      .name = "Steam Deck - keyboard",
      /* This is the LCD model (jupiter).
       * Steam Deck OLED (galileo, possibly pre-production) has
       * .eviocgname = "Valve Software Steam Controller"
       * .version = 0x0110
       * .usb_device_version = 0x0300
       * but is otherwise equivalent.
       */
      .eviocgname = "Valve Software Steam Deck Controller",
      .usb_vendor_name = "Valve Software",
      .usb_product_name = "Steam Deck Controller",
      .bus_type = 0x0003,
      .vendor_id = 0x28de,
      .product_id = 0x1205,
      .version = 0x0110,
      .usb_device_version = 0x0300,
      /* SYN, KEY, MSC, REP */
      .ev = { 0x13, 0x00, 0x10 },
      .keys = {
        /* 0x00 */ 0xfe, 0xff, 0xff, 0xff, FFx4,
        /* 0x40 */ 0xff, 0xff, 0xcf, 0x01, 0xdf, 0xff, 0x80, 0xe0,
      },
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_KEYBOARD
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      .hid_report_descriptor_length = sizeof (steam_deck_kb_hid_report_descriptor),
      .hid_report_descriptor = &steam_deck_kb_hid_report_descriptor[0],
    },
    {
      .name = "Steam Deck LCD - gamepad",
      .eviocgname = "Valve Software Steam Deck Controller",
      .usb_vendor_name = "Valve Software",
      .usb_product_name = "Steam Deck Controller",
      .bus_type = 0x0003,
      .vendor_id = 0x28de,
      .product_id = 0x1205,
      .version = 0x0111,
      .usb_device_version = 0x0200,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* TODO: The data I have for Steam Deck LCD didn't seem to have
       * an evdev device available, so this is extrapolated from
       * kernel source code as being the same as the OLED model
       * (the kernel driver doesn't distinguish). */
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, RX, RY, hat 0-2 x/y */
      .abs = { 0x1b, 0x00, 0x3f },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 0x120 0x46: joystick THUMB, THUMB2, BASE */
          /* 0x130 0xdb: gamepad ABXY, TL/TR */
          /* 0x138 0x7f: gamepad TL2/TR2, SELECT/START, MODE, THUMBL/R */
          /* 0x100 */ ZEROx4, 0x46, 0x00, 0xdb, 0x7f,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x220 0x0f: dpad up/down/left/right */
          /* 0x200 */ ZEROx4, 0x0f, 0x00, 0x00, 0x00,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* 0x2c0 0x0f: joystick TRIGGER_HAPPY1..TRIGGER_HAPPY4 */
          /* 0x2c0 */ 0x0f,
      },
      .hid_report_descriptor_length = sizeof (steam_deck_lcd_js_hid_report_descriptor),
      .hid_report_descriptor = &steam_deck_lcd_js_hid_report_descriptor[0],
    },
    {
      .name = "Steam Deck OLED - gamepad",
      .eviocgname = "Valve Software Steam Controller",
      .usb_vendor_name = "Valve Software",
      .usb_product_name = "Steam Controller",
      .bus_type = 0x0003,
      .vendor_id = 0x28de,
      .product_id = 0x1205,
      .version = 0x0110,
      .usb_device_version = 0x0300,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, RX, RY, hat 0-2 x/y */
      .abs = { 0x1b, 0x00, 0x3f },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 0x120 0x46: joystick THUMB, THUMB2, BASE */
          /* 0x130 0xdb: gamepad ABXY, TL/TR */
          /* 0x138 0x7f: gamepad TL2/TR2, SELECT/START, MODE, THUMBL/R */
          /* 0x100 */ ZEROx4, 0x46, 0x00, 0xdb, 0x7f,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x220 0x0f: dpad up/down/left/right */
          /* 0x200 */ ZEROx4, 0x0f, 0x00, 0x00, 0x00,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* 0x2c0 0x0f: joystick TRIGGER_HAPPY1..TRIGGER_HAPPY4 */
          /* 0x2c0 */ 0x0f,
      },
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .hid_report_descriptor_length = sizeof (steam_deck_oled_js_hid_report_descriptor),
      .hid_report_descriptor = &steam_deck_oled_js_hid_report_descriptor[0],
    },
    {
      .name = "Steam Input virtual controller",
      .eviocgname = "Microsoft X-Box 360 pad 0",
      .bus_type = 0x0003,
      .vendor_id = 0x28de,
      .product_id = 0x11ff,
      .version = 0x0001,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, FF */
      .ev = { 0x0b, 0x00, 0x20 },
      /* XYZ, RXYZ, hat 0 */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 0x130 0xdb: gamepad ABXY, TL/TR */
          /* 0x138 0x7f: gamepad SELECT/START, MODE, THUMBL/R */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    {
      .name = "Guitar Hero for PS3",
      /* SWITCH CO.,LTD. Controller (Dinput) off-brand N64-style USB controller
       * 0003:2563:0575 v0111 is functionally equivalent.
       * https://linux-hardware.org/?id=usb:2563-0575 reports the same IDs as
       * ShenZhen ShanWan Technology ZD-V+ Wired Gaming Controller */
      .bus_type = 0x0003,
      .vendor_id = 0x12ba,
      .product_id = 0x0100,
      .version = 0x0110,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RZ, HAT0X, HAT0Y */
      .abs = { 0x27, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, SELECT, START, MODE */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x1f,
      },
    },
    {
      .name = "G27 Racing Wheel, 0003:046d:c29b v0111",
      .bus_type = 0x0003,
      .vendor_id = 0x046d,
      .product_id = 0xc29b,
      .version = 0x0111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RZ, HAT0X, HAT0Y */
      .abs = { 0x27, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 16 buttons: TRIGGER, THUMB, THUMB2, TOP, TOP2, PINKIE, BASE,
           * BASE2..BASE6, unregistered event codes 0x12c-0x12e, DEAD */
          /* 0x100 */ ZEROx4, 0xff, 0xff, 0x00, 0x00,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* TRIGGER_HAPPY1..TRIGGER_HAPPY7 */
          /* 0x2c0 */ 0x7f,
      },
    },
    {
      .name = "Logitech Driving Force, 0003:046d:c294 v0100",
      .bus_type = 0x0003,
      .vendor_id = 0x046d,
      .product_id = 0xc294,
      .version = 0x0100,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, RZ, HAT0X, HAT0Y */
      .abs = { 0x23, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 12 buttons: TRIGGER, THUMB, THUMB2, TOP, TOP2, PINKIE, BASE,
           * BASE2..BASE6 */
          /* 0x100 */ ZEROx4, 0xff, 0x0f, 0x00, 0x00,
      },
    },
    {
      .name = "Logitech Dual Action",
      .bus_type = 0x0003,
      .vendor_id = 0x046d,
      .product_id = 0xc216,
      .version = 0x0110,
      /* Logitech RumblePad 2 USB, 0003:046d:c218 v0110, is the same
       * except for having force feedback, which we don't use in our
       * heuristic */
      /* Jess Tech GGE909 PC Recoil Pad, 0003:0f30:010b v0110, is the same */
      /* 8BitDo SNES30 via USB, 0003:2dc8:ab20 v0110, is the same;
       * see below for the same physical device via Bluetooth,
       * 0005:2dc8:2840 v0100 */
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RZ, HAT0X, HAT0Y */
      .abs = { 0x27, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 12 buttons: TRIGGER, THUMB, THUMB2, TOP, TOP2, PINKIE, BASE,
           * BASE2..BASE6 */
          /* 0x100 */ ZEROx4, 0xff, 0x0f, 0x00, 0x00,
      },
    },
    {
      .name = "8BitDo SNES30 v0100 via Bluetooth",
      .eviocgname = "8Bitdo SNES30 GamePad",
      /* The same physical device via USB, 0003:2dc8:ab20 v0110,
       * is reported differently (above). */
      /* 8BitDo NES30 Pro (aka N30 Pro) via Bluetooth, 0005:2dc8:3820 v0100,
       * is functionally equivalent; but the same physical device via USB,
       * 0003:2dc8:9001 v0111, matches N30 Pro 2 v0111. */
      .bus_type = 0x0005,
      .vendor_id = 0x2dc8,
      .product_id = 0x2840,
      .version = 0x0100,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC */
      .ev = { 0x1b },
      /* XYZ, RZ, GAS, BRAKE, HAT0X, HAT0Y */
      .abs = { 0x27, 0x06, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, SELECT, START, MODE, THUMBL, THUMBR,
           * and an unassigned button code */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0xff,
      },
    },
    {
      .name = "Saitek ST290 Pro flight stick",
      .bus_type = 0x0003,
      .vendor_id = 0x06a3,
      .product_id = 0x0160,   /* 0x0460 seems to be the same */
      .version = 0x0100,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC */
      .ev = { 0x1b },
      /* X, Y, Z, RZ, HAT0X, HAT0Y */
      .abs = { 0x27, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* TRIGGER, THUMB, THUMB2, TOP, TOP2, PINKIE */
          /* 0x100 */ ZEROx4, 0x3f, 0x00, 0x00, 0x00,
      },
    },
    {
      .name = "Saitek X52 Pro Flight Control System",
      .bus_type = 0x0003,
      .vendor_id = 0x06a3,
      .product_id = 0x0762,
      .version = 0x0111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .ev = { 0x0b },
      /* XYZ, RXYZ, throttle, hat0, MISC, unregistered event code 0x29 */
      .abs = { 0x7f, 0x00, 0x03, 0x00, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 16 buttons: TRIGGER, THUMB, THUMB2, TOP, TOP2, PINKIE, BASE,
           * BASE2..BASE6, unregistered event codes 0x12c-0x12e, DEAD */
          /* 0x100 */ ZEROx4, 0xff, 0xff, 0x00, 0x00,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* TRIGGER_HAPPY1..TRIGGER_HAPPY23 */
          /* 0x2c0 */ 0xff, 0xff, 0x7f,
      },
    },
    {
      .name = "Logitech Extreme 3D",
      .bus_type = 0x0003,
      .vendor_id = 0x046d,
      .product_id = 0xc215,
      .version = 0x0110,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC */
      .ev = { 0x0b },
      /* X, Y, RZ, throttle, hat 0 */
      .abs = { 0x63, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 12 buttons: TRIGGER, THUMB, THUMB2, TOP, TOP2, PINKIE, BASE,
           * BASE2..BASE6 */
          /* 0x100 */ ZEROx4, 0xff, 0x0f, 0x00, 0x00,
      },
    },
    {
      .name = "Hori Real Arcade Pro VX-SA",
      .bus_type = 0x0003,
      .vendor_id = 0x24c6,
      .product_id = 0x5501,
      .version = 0x0533,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, hat 0 */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    {
      /* https://github.com/ValveSoftware/steam-devices/pull/42
       * PS4 mode is functionally equivalent, but with product ID 0x011c
       * and version 0x1101. */
      .name = "Hori Fighting Stick Alpha - PS5 mode",
      .bus_type = 0x0003,   /* USB */
      .vendor_id = 0x0f0d,  /* Hori Co., Ltd. */
      .product_id = 0x0184, /* HORI FIGHTING STICK α (PS5 mode) */
      .version = 0x0111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC */
      .ev = { 0x1b },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x3f,
      },
    },
    {  /* https://github.com/ValveSoftware/steam-devices/pull/42 */
      .name = "Hori Fighting Stick Alpha - PC mode",
      .bus_type = 0x0003,   /* USB */
      .vendor_id = 0x0f0d,  /* Hori Co., Ltd. */
      .product_id = 0x011e, /* HORI FIGHTING STICK α (PC mode) */
      .version = 0x0116,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, FF */
      .ev = { 0x0b, 0x00, 0x20 },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    {  /* https://github.com/ValveSoftware/steam-devices/issues/29 */
      .name = "HORIPAD S for Nintendo",
      .bus_type = 0x0003,   /* USB */
      .vendor_id = 0x0f0d,  /* Hori Co., Ltd. */
      .product_id = 0x00dc, /* HORIPAD S */
      .version = 0x0112,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, FF */
      .ev = { 0x0b, 0x00, 0x20 },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    {
      .name = "Switch Pro Controller via Bluetooth",
      .bus_type = 0x0005,
      .vendor_id = 0x057e,
      .product_id = 0x2009,
      .version = 0x0001,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, RX, RY, hat 0 */
      .abs = { 0x1b, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 16 buttons: TRIGGER, THUMB, THUMB2, TOP, TOP2, PINKIE, BASE,
           * BASE2..BASE6, unregistered event codes 0x12c-0x12e, DEAD */
          /* 0x100 */ ZEROx4, 0xff, 0xff, 0x00, 0x00,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* TRIGGER_HAPPY1..TRIGGER_HAPPY2 */
          /* 0x2c0 */ 0x03,
      },
    },
    {
      .name = "Switch Pro Controller via Bluetooth (Linux 6.2.11)",
      .eviocgname = "Pro Controller",
      .bus_type = 0x0005,
      .vendor_id = 0x057e,
      .product_id = 0x2009,
      .version = 0x0001,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, RX, RY, hat 0 */
      .abs = { 0x1b, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, SELECT, START, MODE, THUMBL, THUMBR,
           * and an unassigned button code */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0xff,
      },
    },
    {
      .name = "Switch Pro Controller via USB",
      .eviocgname = "Nintendo Co., Ltd. Pro Controller",
      .usb_vendor_name = "Nintendo Co., Ltd.",
      .usb_product_name = "Pro Controller",
      .bus_type = 0x0003,
      .vendor_id = 0x057e,
      .product_id = 0x2009,
      .version = 0x0111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, Z, RZ, HAT0X, HAT0Y */
      .abs = { 0x27, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* 16 buttons: TRIGGER, THUMB, THUMB2, TOP, TOP2, PINKIE, BASE,
           * BASE2..BASE6, unregistered event codes 0x12c-0x12e, DEAD */
          /* 0x100 */ ZEROx4, 0xff, 0xff, 0x00, 0x00,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* TRIGGER_HAPPY1..TRIGGER_HAPPY2 */
          /* 0x2c0 */ 0x03,
      },
    },
    { /* https://github.com/ValveSoftware/steam-devices/pull/40 */
      .name = "PDP wired Pro Controller for Switch",
      /* 0003:0e6f:0184 "Performance Designed Products" /
       * "Faceoff Deluxe+ Audio Wired Controller for Nintendo Switch" appears
       * to be functionally equivalent */
      .eviocgname = "PDP CO.,LTD. Faceoff Wired Pro Controller for Nintendo Switch",
      .usb_vendor_name = "PDP CO.,LTD.",
      .usb_product_name = "Faceoff Wired Pro Controller for Nintendo Switch",
      .bus_type = 0x0003,
      .vendor_id = 0x0e6f,
      .product_id = 0x0180,
      .version = 0x0111,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC */
      .ev = { 0x1b },
      /* X, Y, Z, RZ, HAT0X, HAT0Y */
      .abs = { 0x27, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x3f,
      },
    },
    {
      .name = "NES Controller (R) NES-style Joycon from Nintendo Online",
      .eviocgname = "NES Controller (R)",
      /* Joy-Con (L), 0005:057e:2006 v0001, is functionally equivalent.
       * Ordinary Joy-Con (R) and NES-style Joy-Con (L) are assumed to be
       * functionally equivalent as well. */
      .bus_type = 0x0005,   /* Bluetooth-only */
      .vendor_id = 0x057e,
      .product_id = 0x2007,
      .version = 0x0001,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, RX, RY, hat 0 */
      .abs = { 0x1b, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, SELECT, START, MODE, THUMBL, THUMBR,
           * and an unassigned button code */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0xff,
      },
    },
    {
      .name = "Thrustmaster Racing Wheel FFB",
      /* Several devices intended for PS4 are functionally equivalent to this:
       * https://github.com/ValveSoftware/steam-devices/pull/34
       * Mad Catz FightStick TE S+ PS4, 0003:0738:8384:0111 v0111
       * (aka Street Fighter V Arcade FightStick TES+)
       * Mad Catz FightStick TE2+ PS4, 0003:0738:8481 v0100
       * (aka Street Fighter V Arcade FightStick TE2+)
       * Bigben Interactive DAIJA Arcade Stick, 0003:146b:0d09 v0111
       * (aka Nacon Daija PS4 Arcade Stick)
       * Razer Razer Raiju Ultimate Wired, 0003:1532:1009 v0000
       * Razer Razer Raiju Ultimate (Bluetooth), 0005:1532:1009 v0001
       */
      .bus_type = 0x0003,
      .vendor_id = 0x044f,
      .product_id = 0xb66d,
      .version = 0x0110,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .ev = { 0x0b },
      /* XYZ, RXYZ, hat 0 */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x3f,
      },
    },
    {
      .name = "Thrustmaster T.Flight Hotas X",
      .bus_type = 0x0003,
      .vendor_id = 0x044f,
      .product_id = 0xb108,
      .version = 0x0100,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .ev = { 0x0b },
      /* XYZ, RZ, throttle, hat 0 */
      .abs = { 0x67, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* trigger, thumb, thumb2, top, top2, pinkie, base,
           * base2..base6 */
          /* 0x100 */ ZEROx4, 0xff, 0x0f
      },
    },
    {
      .name = "8BitDo N30 Pro via Bluetooth",
      /* This device has also been seen reported with an additional
       * unassigned button code, the same as the SNES30 v0100 via Bluetooth */
      .bus_type = 0x0005,
      .vendor_id = 0x2dc8,
      .product_id = 0x3820,
      .version = 0x0100,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS, MSC */
      .ev = { 0x1b },
      /* XYZ, RZ, gas, brake, hat0 */
      .abs = { 0x27, 0x06, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, select, start, mode, thumbl,
           * thumbr */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x7f,
      },
    },
    {
      .name = "8BitDo N30 Pro 2 v0111",
      .bus_type = 0x0003,
      .vendor_id = 0x2dc8,
      .product_id = 0x9015,
      .version = 0x0111,
      /* 8BitDo NES30 Pro via USB, 0003:2dc8:9001 v0111, is the same;
       * but the same physical device via Bluetooth, 0005:2dc8:3820 v0100,
       * matches 8BitDo SNES30 v0100 via Bluetooth instead (see above). */
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .ev = { 0x0b },
      /* XYZ, RZ, gas, brake, hat0 */
      .abs = { 0x27, 0x06, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, select, start, mode, thumbl,
           * thumbr */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x7f,
      },
    },
    {
      .name = "8BitDo N30 Pro 2 via Bluetooth",
      /* Physically the same device as the one that mimics an Xbox 360
       * USB controller when wired */
      .bus_type = 0x0005,
      .vendor_id = 0x045e,
      .product_id = 0x02e0,
      .version = 0x0903,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      /* SYN, KEY, ABS, MSC, FF */
      .ev = { 0x1b, 0x00, 0x20 },
      /* X, Y, Z, RX, RY, RZ, HAT0X, HAT0Y */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx8,
          /* KEY_MENU */
          /* 0x80 */ 0x00, 0x08, 0x00, 0x00, ZEROx4,
          /* 0xc0 */ ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2 */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x03,
      },
    },
    {
      .name = "Retro Power SNES-style controller, 0003:0079:0011 v0110",
      .bus_type = 0x0003,
      .vendor_id = 0x0079,
      .product_id = 0x0011,
      .version = 0x0110,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .ev = { 0x0b },
      /* X, Y */
      .abs = { 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* trigger, thumb, thumb2, top, top2, pinkie, base,
           * base2..base4 */
          /* 0x100 */ ZEROx4, 0xff, 0x03, 0x00, 0x00,
      },
    },
    {
      .name = "Google Stadia Controller rev.A",
      .eviocgname = "Google LLC Stadia Controller rev. A",
      .usb_vendor_name = "Google LLC",
      .usb_product_name = "Stadia Controller rev. A",
      /* This data is with USB-C, but the same physical device via Bluetooth,
       * 0005:18d1:9400 v0000, is functionally equivalent other than having
       * EVIOCGNAME = StadiaXXXX-YYYY where XXXX is the last 4 digits of
       * the serial number and YYYY is some other mystery number */
      .bus_type = 0x0003,
      .vendor_id = 0x18d1,
      .product_id = 0x9400,
      .version = 0x0100,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      .ev = { 0x0b },
      /* XYZ, RZ, gas, brake, hat0 */
      .abs = { 0x27, 0x06, 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* Volume up/down */
          /* 0x40 */ ZEROx4, 0x00, 0x00, 0x0c, 0x00,
          /* Media play/pause */
          /* 0x80 */ ZEROx4, 0x10, 0x00, 0x00, 0x00,
          /* 0xc0 */ ZEROx8,
          /* ABXY, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ ZEROx8,
          /* 0x280 */ ZEROx8,
          /* TRIGGER_HAPPY 1-4 */
          /* 0x2c0 */ 0x0f, 0x00, 0x00, 0x00, ZEROx4,
      },
    },
    {
      .name = "Microsoft Xbox Series S|X Controller (model 1914) via USB",
      .eviocgname = "Microsoft Xbox Series S|X Controller",
      .usb_vendor_name = "Microsoft",
      .usb_product_name = "Controller",
      /* Physically the same device as 0003:045e:0b13 v0515 below,
       * but some functionality is mapped differently */
      .bus_type = 0x0003,
      .vendor_id = 0x045e,
      .product_id = 0x0b12,
      .version = 0x050f,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      .ev = { 0x0b },
      /* X, Y, Z, RX, RY, RZ, hat 0 */
      .abs = { 0x3f, 0x00, 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx8,
          /* Record */
          /* 0x80 */ ZEROx4, 0x80, 0x00, 0x00, 0x00,
          /* 0xc0 */ ZEROx8,
          /* ABXY, TL, TR, SELECT, START, MODE, THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7c,
      },
    },
    {
      .name = "Microsoft Xbox Series S|X Controller (model 1914) via Bluetooth",
      .eviocgname = "Xbox Wireless Controller",
      /* Physically the same device as 0003:045e:0b12 v050f above,
       * but some functionality is mapped differently */
      .bus_type = 0x0005,
      .vendor_id = 0x045e,
      .product_id = 0x0b13,
      .version = 0x0515,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      .ev = { 0x0b },
      /* XYZ, RZ, gas, brake, hat0 */
      .abs = { 0x27, 0x06, 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx8,
          /* Record */
          /* 0x80 */ ZEROx4, 0x80, 0x00, 0x00, 0x00,
          /* 0xc0 */ ZEROx8,
          /* ABC, XYZ, TL, TR, TL2, TR2, select, start, mode, thumbl,
           * thumbr */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xff, 0x7f,
      },
    },
    {
      .name = "Wiimote - buttons",
      .eviocgname = "Nintendo Wii Remote",
      .bus_type = 0x0005,
      .vendor_id = 0x057e,
      .product_id = 0x0306,
      .version = 0x0600,
      /* This one is a bit weird because some of the buttons are mapped
       * to the arrow, page up and page down keys, so it's a joystick
       * with a subset of a keyboard attached. */
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      /* SYN, KEY, FF */
      .ev = { 0x03, 0x00, 0x20 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* left, right, up down */
          /* 0x40 */ ZEROx4, 0x80, 0x16, 0x00, 0x00,
          /* 0x80 */ ZEROx8,
          /* 0xc0 */ ZEROx8,
          /* BTN_1, BTN_2, BTN_A, BTN_B, BTN_MODE */
          /* 0x100 */ 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x10,
          /* 0x140 */ ZEROx8,
          /* next (keyboard page down), previous (keyboard page up) */
          /* 0x180 */ 0x00, 0x00, 0x80, 0x10, ZEROx4,
      },
    },
    {
      .name = "Wiimote - accelerometer",
      .eviocgname = "Nintendo Wii Remote Accelerometer",
      .bus_type = 0x0005,
      .vendor_id = 0x057e,
      .product_id = 0x0306,
      .version = 0x0600,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER,
      /* SYN, ABS */
      .ev = { 0x09 },
      /* RX, RY, RZ - even though it would more conventionally be X, Y, Z */
      .abs = { 0x38 },
    },
    {
      .name = "Wiimote - Motion Plus gyroscope",
      .eviocgname = "Nintendo Wii Remote Motion Plus",
      /* Note that if we only look at the bus type, vendor, product, version
       * and axes, this is indistinguishable from the accelerometer */
      .bus_type = 0x0005,
      .vendor_id = 0x057e,
      .product_id = 0x0306,
      .version = 0x0600,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER,
      /* SYN, ABS */
      .ev = { 0x09 },
      /* RX, RY, RZ */
      .abs = { 0x38 },
    },
    {
      .name = "Wiimote - IR positioning",
      .eviocgname = "Nintendo Wii Remote IR",
      .bus_type = 0x0005,
      .vendor_id = 0x057e,
      .product_id = 0x0306,
      .version = 0x0600,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, ABS */
      .ev = { 0x09 },
      /* HAT0X, Y to HAT3X, Y */
      .abs = { 0x00, 0x00, 0xff },
    },
    {
      .name = "Wiimote - Nunchuck",
      .eviocgname = "Nintendo Wii Remote Nunchuk",
      .bus_type = 0x0005,
      .vendor_id = 0x057e,
      .product_id = 0x0306,
      .version = 0x0600,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* RX, RY, RZ, hat 0 - even though this is an accelerometer, which
       * would more conventionally be X, Y, Z, and a left joystick, which
       * would more conventionally be X, Y */
      .abs = { 0x38, 0x00, 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
         /* C and Z buttons */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0x24, 0x00,
      },
    },
    {
      .name = "Wiimote - Classic Controller",
      .eviocgname = "Nintendo Wii Remote Classic Controller",
      .bus_type = 0x0005,
      .vendor_id = 0x057e,
      .product_id = 0x0306,
      .version = 0x0600,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* Hat 1-3 X and Y */
      .abs = { 0x00, 0x00, 0xfc },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* left, right, up down */
          /* 0x40 */ ZEROx4, 0x80, 0x16, 0x00, 0x00,
          /* 0x80 */ ZEROx8,
          /* 0xc0 */ ZEROx8,
          /* A, B, X, Y, MODE, TL, TL2, TR, TR2 */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x13,
          /* 0x140 */ ZEROx8,
          /* next (keyboard page down), previous (keyboard page up) */
          /* 0x180 */ 0x00, 0x00, 0x80, 0x10, ZEROx4,
      },
    },
    {
      /* Flags guessed from kernel source code, not confirmed with real
       * hardware */
      .name = "Wiimote - Balance Board",
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* Hat 0-1 */
      .abs = { 0x00, 0x00, 0x0f },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* BTN_A */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0x01, 0x00,
      },
    },
    {
      /* Flags guessed from kernel source code, not confirmed with real
       * hardware */
      .name = "Wiimote - Wii U Pro Controller",
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, RX, RY */
      .abs = { 0x1b },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* A, B, X, Y, TL, TR, TL2, TR2, SELECT, START, MODE,
           * THUMBL, THUMBR */
          /* 0x100 */ ZEROx4, 0x00, 0x00, 0xdb, 0x7f,
          /* 0x140 */ ZEROx8,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ ZEROx8,
          /* Digital dpad */
          /* 0x200 */ ZEROx4, 0x0f, 0x00, 0x00, 0x00,
      },
    },
    {
      .name = "Synaptics TM3381-002 (Thinkpad X280 trackpad)",
      .eviocgname = "Synaptics TM3381-002",
      .bus_type = 0x001d,   /* BUS_RMI */
      .vendor_id = 0x06cb,
      .product_id = 0x0000,
      .version = 0x0000,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHPAD,
      /* SYN, KEY, ABS */
      .ev = { 0x0b },
      /* X, Y, pressure, multitouch */
      .abs = { 0x03, 0x00, 0x00, 0x01, 0x00, 0x80, 0xf3, 0x06 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* Left mouse button */
          /* 0x100 */ 0x00, 0x00, 0x01, 0x00, ZEROx4,
          /* BTN_TOOL_FINGER and some multitouch gestures */
          /* 0x140 */ 0x20, 0xe5
      },
      /* POINTER, BUTTONPAD */
      .props = { 0x05 },
    },
    {
      .name = "DELL08AF:00 (Dell XPS laptop touchpad)",
      .bus_type = 0x18,
      .vendor_id = 0x6cb,
      .product_id = 0x76af,
      .version = 0x100,
      .ev = { 0x0b },
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHPAD,
      /* X, Y, multitouch */
      .abs = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0xe0, 0x02 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* Left mouse button */
          /* 0x100 */ 0x00, 0x00, 0x01, 0x00, ZEROx4,
          /* BTN_TOOL_FINGER and some multitouch gestures */
          /* 0x140 */ 0x20, 0xe5
      },
      /* POINTER, BUTTONPAD */
      .props = { 0x05 },
    },
    {
      .name = "TPPS/2 Elan TrackPoint (Thinkpad X280)",
      .eviocgname = "TPPS/2 Elan TrackPoint",
      .bus_type = 0x0011,   /* BUS_I8042 */
      .vendor_id = 0x0002,
      .product_id = 0x000a,
      .version = 0x0000,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_POINTING_STICK,
      /* SYN, KEY, REL */
      .ev = { 0x07 },
      /* X, Y */
      .rel = { 0x03 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* Left, middle, right mouse buttons */
          /* 0x100 */ 0x00, 0x00, 0x07,
      },
      /* POINTER, POINTING_STICK */
      .props = { 0x21 },
    },
    {
      .name = "Thinkpad ACPI buttons",
      .eviocgname = "ThinkPad Extra Buttons",
      .bus_type = 0x0019,
      .vendor_id = 0x17aa,
      .product_id = 0x5054,
      .version = 0x4101,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS |
                   SRT_INPUT_DEVICE_TYPE_FLAGS_SWITCH),
      /* SYN, KEY, MSC, SW */
      .ev = { 0x33 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx4, 0x00, 0x00, 0x0e, 0x01,
          /* 0x80 */ 0x00, 0x50, 0x11, 0x51, 0x00, 0x28, 0x00, 0xc0,
          /* 0xc0 */ 0x04, 0x20, 0x10, 0x02, 0x1b, 0x70, 0x01, 0x00,
          /* 0x100 */ ZEROx8,
          /* 0x140 */ ZEROx4, 0x00, 0x00, 0x50, 0x00,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ 0x00, 0x00, 0x04, 0x18, ZEROx4,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ 0x40, 0x00, 0x01, 0x00, ZEROx4,
      },
    },
    {
      .name = "Thinkpad ACPI buttons (Linux 6.1)",
      .eviocgname = "ThinkPad Extra Buttons",
      .bus_type = 0x0019,
      .vendor_id = 0x17aa,
      .product_id = 0x5054,
      .version = 0x4101,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_SWITCH),
      /* SYN, KEY, MSC, SW */
      .ev = { 0x33 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx4, 0x00, 0x00, 0x0e, 0x01,
          /* 0x80 */ 0x00, 0x50, 0x11, 0x51, 0x00, 0x28, 0x00, 0xc0,
          /* 0xc0 */ 0x04, 0x20, 0x10, 0x02, 0x1b, 0x70, 0x01, 0x00,
          /* 0x100 */ ZEROx8,
          /* 0x140 */ ZEROx4, 0x00, 0x00, 0x50, 0x00,
          /* 0x180 */ ZEROx4, 0x00, 0x00, 0x00, 0x70,
          /* 0x1c0 */ 0x00, 0x00, 0x04, 0x18, 0x20, 0x00, 0x00, 0x00,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ ZEROx8,
      },
    },
    {
      .name = "PC speaker",
      .eviocgname = "PC Speaker",
      .bus_type = 0x0010,   /* BUS_ISA */
      .vendor_id = 0x001f,
      .product_id = 0x0001,
      .version = 0x0100,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_NONE,
      /* SYN, SND */
      .ev = { 0x01, 0x00, 0x04 },
    },
    {
      .name = "HDA Digital PCBeep",
      .eviocgname = "HDA Digital PCBeep",
      .bus_type = 0x0001,
      .vendor_id = 0x10ec,
      .product_id = 0x0257,
      .version = 0x0001,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_NONE,
      /* SYN, SND */
      .ev = { 0x01, 0x00, 0x04 },
    },
    {
      .name = "ALSA headphone detection, etc.",
      .eviocgname = "HDA Intel PCH Mic",
      /* HDA Intel PCH Headphone is functionally equivalent */
      /* HDA Intel PCH HDMI/DP,pcm=3 is functionally equivalent */
      /* HDA Intel PCH HDMI/DP,pcm=7 is functionally equivalent */
      /* HDA Intel PCH HDMI/DP,pcm=8 is functionally equivalent */
      .bus_type = 0x0000,
      .vendor_id = 0x0000,
      .product_id = 0x0000,
      .version = 0x0000,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_SWITCH,
      /* SYN, SW */
      .ev = { 0x21 },
    },
    {
      /* Assumed to be a reasonably typical i8042 (PC AT) keyboard */
      .name = "Thinkpad T520 and X280 keyboards",
      /* Steam Deck LCD/OLED keyboard interface is version 0xab83 but
       * otherwise equivalent */
      .eviocgname = "AT Translated Set 2 keyboard",
      .bus_type = 0x0011,   /* BUS_I8042 */
      .vendor_id = 0x0001,
      .product_id = 0x0001,
      .version = 0xab54,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_KEYBOARD
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      /* SYN, KEY, MSC, LED, REP */
      .ev = { 0x13, 0x00, 0x12 },
      .keys = {
          /* 0x00 */ 0xfe, 0xff, 0xff, 0xff, FFx4,
          /* 0x40 */ 0xff, 0xff, 0xef, 0xff, 0xdf, 0xff, 0xff, 0xfe,
          /* 0x80 */ 0x01, 0xd0, 0x00, 0xf8, 0x78, 0x30, 0x80, 0x03,
          /* 0xc0 */ 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00,
      },
    },
    {
      .name = "Thinkpad X280 sleep button",
      .eviocgname = "Sleep Button",
      .bus_type = 0x0019,   /* BUS_HOST */
      .vendor_id = 0x0000,
      .product_id = 0x0003,
      .version = 0x0000,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS,
      /* SYN, KEY */
      .ev = { 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx8,
          /* KEY_SLEEP */
          /* 0x80 */ 0x00, 0x40,
      },
    },
    {
      /* As seen on Thinkpad X280, Steam Deck LCD, Steam Deck OLED */
      .name = "ACPI lid switch",
      .eviocgname = "Lid Switch",
      .bus_type = 0x0019,   /* BUS_HOST */
      .vendor_id = 0x0000,
      .product_id = 0x0005,
      .version = 0x0000,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_SWITCH,
      /* SYN, SW */
      .ev = { 0x21 },
    },
    {
      /* As seen on Thinkpad X280, Steam Deck LCD, Steam Deck OLED */
      .name = "ACPI power button",
      .eviocgname = "Power Button",
      .bus_type = 0x0019,   /* BUS_HOST */
      .vendor_id = 0x0000,
      .product_id = 0x0001,
      .version = 0x0000,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS,
      /* SYN, KEY */
      .ev = { 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* KEY_POWER */
          /* 0x40 */ ZEROx4, 0x00, 0x00, 0x10, 0x00,
      },
    },
    {
      /* As seen on Thinkpad X280, Steam Deck LCD, Steam Deck OLED */
      .name = "ACPI video bus",
      .eviocgname = "Video Bus",
      .bus_type = 0x0019,   /* BUS_HOST */
      .vendor_id = 0x0000,
      .product_id = 0x0006,
      .version = 0x0000,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS,
      /* SYN, KEY */
      .ev = { 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx8,
          /* 0x80 */ ZEROx8,
          /* brightness control, video mode, display off */
          /* 0xc0 */ ZEROx4, 0x0b, 0x00, 0x3e, 0x00,
      },
    },
    {
      .name = "Thinkpad X280 webcam",
      .eviocgname = "Integrated Camera: Integrated C",
      .usb_vendor_name = "Chicony Electronics Co.,Ltd.",
      .usb_product_name = "Integrated Camera",
      .bus_type = 0x0003,
      .vendor_id = 0x04f2,
      .product_id = 0xb604,
      .version = 0x0027,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS,
      /* SYN, KEY */
      .ev = { 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx8,
          /* 0x80 */ ZEROx8,
          /* KEY_CAMERA */
          /* 0xc0 */ 0x00, 0x00, 0x10, 0x00, ZEROx4,
      },
    },
    {
      .name = "Thinkpad X280 extra buttons",
      .bus_type = 0x0019,   /* BUS_HOST */
      .vendor_id = 0x17aa,
      .product_id = 0x5054,
      .version = 0x4101,
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS,
      /* SYN, KEY */
      .ev = { 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx4, 0x00, 0x00, 0x0e, 0x01,
          /* 0x80 */ 0x00, 0x50, 0x11, 0x51, 0x00, 0x28, 0x00, 0xc0,
          /* 0xc0 */ 0x04, 0x20, 0x10, 0x02, 0x1b, 0x70, 0x01, 0x00,
          /* 0x100 */ ZEROx8,
          /* 0x140 */ ZEROx4, 0x00, 0x00, 0x50, 0x00,
          /* 0x180 */ ZEROx8,
          /* 0x1c0 */ 0x00, 0x00, 0x04, 0x18, ZEROx4,
          /* 0x200 */ ZEROx8,
          /* 0x240 */ 0x40, 0x00, 0x01, 0x00, ZEROx4,
      },
    },
    {
      .name = "Thinkpad USB keyboard with Trackpoint - keyboard",
      .eviocgname = "Lite-On Technology Corp. ThinkPad USB Keyboard with TrackPoint",
      .usb_vendor_name = "Lite-On Technology Corp.",
      .usb_product_name = "ThinkPad USB Keyboard with TrackPoint",
      .bus_type = 0x0003,
      .vendor_id = 0x17ef,
      .product_id = 0x6009,
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_KEYBOARD
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      /* SYN, KEY, MSC, LED, REP */
      .ev = { 0x13, 0x00, 0x12 },
      .keys = {
          /* 0x00 */ 0xfe, 0xff, 0xff, 0xff, FFx4,
          /* 0x40 */ 0xff, 0xff, 0xef, 0xff, 0xdf, 0xff, 0xbe, 0xfe,
          /* 0x80 */ 0xff, 0x57, 0x40, 0xc1, 0x7a, 0x20, 0x9f, 0xff,
          /* 0xc0 */ 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
      },
      .hid_report_descriptor_length = sizeof (thinkpad_usb_keyboard_hid_report_descriptor),
      .hid_report_descriptor = &thinkpad_usb_keyboard_hid_report_descriptor[0],
    },
    {
      .name = "Thinkpad USB keyboard with Trackpoint - Trackpoint",
      .eviocgname = "Lite-On Technology Corp. ThinkPad USB Keyboard with TrackPoint",
      .usb_vendor_name = "Lite-On Technology Corp.",
      .usb_product_name = "ThinkPad USB Keyboard with TrackPoint",
      .bus_type = 0x0003,
      .vendor_id = 0x17ef,
      .product_id = 0x6009,
      .version = 0x0110,
      /* For some reason the special keys like mute and wlan toggle
       * show up here instead of, or in addition to, as part of
       * the keyboard - so we report this as having keys too. */
      .expected = (SRT_INPUT_DEVICE_TYPE_FLAGS_MOUSE
                   | SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS),
      /* SYN, KEY, REL, MSC, LED */
      .ev = { 0x17, 0x00, 0x02 },
      /* X, Y */
      .rel = { 0x03 },
      .keys = {
          /* 0x00 */ ZEROx8,
          /* 0x40 */ ZEROx4, 0x00, 0x00, 0x1e, 0x00,
          /* 0x80 */ 0x00, 0xcc, 0x11, 0x01, 0x78, 0x40, 0x00, 0xc0,
          /* 0xc0 */ 0x00, 0x20, 0x10, 0x00, 0x0b, 0x50, 0x00, 0x00,
          /* Mouse buttons: left, right, middle, "task" */
          /* 0x100 */ 0x00, 0x00, 0x87, 0x68, ZEROx4,
          /* 0x140 */ ZEROx4, 0x00, 0x00, 0x10, 0x00,
          /* 0x180 */ ZEROx4, 0x00, 0x00, 0x40, 0x00,
      },
      .hid_report_descriptor_length = sizeof (thinkpad_usb_trackpoint_hid_report_descriptor),
      .hid_report_descriptor = &thinkpad_usb_trackpoint_hid_report_descriptor[0],
    },
    { /* https://github.com/ValveSoftware/Proton/issues/5126 */
      .name = "Smarty Co. VRS DirectForce Pro Pedals",
      .bus_type = 0x0003,
      .vendor_id = 0x0483,  /* STMicroelectronics */
      .product_id = 0xa3be, /* VRS DirectForce Pro Pedals */
      .version = 0x0111,
      /* TODO: Ideally we would identify this as a joystick, but there
       * isn't currently enough information to do that without a table
       * of known devices. */
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .todo = "https://github.com/ValveSoftware/Proton/issues/5126",
      /* SYN, ABS */
      .ev = { 0x09 },
      /* X, Y, Z */
      .abs = { 0x07 },
      .hid_report_descriptor_length = sizeof (vrs_pedals_hid_report_descriptor),
      .hid_report_descriptor = &vrs_pedals_hid_report_descriptor[0],
    },
    { /* https://github.com/ValveSoftware/Proton/issues/5126 */
      .name = "Heusinkveld Heusinkveld Sim Pedals Ultimate",
      .bus_type = 0x0003,
      .vendor_id = 0x30b7,  /* Heusinkveld Engineering */
      .product_id = 0x1003, /* Heusinkveld Sim Pedals Ultimate */
      .version = 0x0000,
      /* TODO: Ideally we would identify this as a joystick, but there
       * isn't currently enough information to do that without a table
       * of known devices. */
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .todo = "https://github.com/ValveSoftware/Proton/issues/5126",
      /* SYN, ABS */
      .ev = { 0x09 },
      /* RX, RY, RZ */
      .abs = { 0x38 },
      .hid_report_descriptor_length = sizeof (heusinkveld_pedals_hid_report_descriptor),
      .hid_report_descriptor = &heusinkveld_pedals_hid_report_descriptor[0],
    },
    { /* https://github.com/ValveSoftware/Proton/issues/5126 */
      .name = "Vitaly [mega_mozg] Naidentsev ODDOR-handbrake",
      .bus_type = 0x0003,
      .vendor_id = 0x0000,
      .product_id = 0x0000,
      .version = 0x0001,
      /* TODO: Ideally we would identify this as a joystick by it having
       * the joystick-specific THROTTLE axis and TRIGGER/THUMB buttons */
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .todo = "https://github.com/ValveSoftware/Proton/issues/5126",
      /* SYN, KEY, ABS, MSC */
      .ev = { 0x1b },
      /* THROTTLE only */
      .abs = { 0x40 },
      .keys = {
          /* 0x00-0xff */ ZEROx8, ZEROx8, ZEROx8, ZEROx8,
          /* TRIGGER = 0x120, THUMB = 0x121 */
          /* 0x100 */ ZEROx4, 0x03, 0x00, 0x00, 0x00,
      },
    },
    { /* https://github.com/ValveSoftware/Proton/issues/5126 */
      .name = "Leo Bodnar Logitech® G25 Pedals",
      .bus_type = 0x0003,
      .vendor_id = 0x1dd2,  /* Leo Bodnar Electronics Ltd */
      .product_id = 0x100c,
      .version = 0x0110,
      /* TODO: Ideally we would identify this as a joystick, but there
       * isn't currently enough information to do that without a table
       * of known devices. */
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .todo = "https://github.com/ValveSoftware/Proton/issues/5126",
      /* SYN, ABS */
      .ev = { 0x09 },
      /* RX, RY, RZ */
      .abs = { 0x38 },
    },
    { /* https://github.com/ValveSoftware/Proton/issues/5126 */
      .name = "FANATEC ClubSport USB Handbrake",
      .bus_type = 0x0003,
      .vendor_id = 0x0eb7,
      .product_id = 0x1a93,
      .version = 0x0111,
      /* TODO: Ideally we would identify this as a joystick, but there
       * isn't currently enough information to do that without a table
       * of known devices. */
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK,
      .todo = "https://github.com/ValveSoftware/Proton/issues/5126",
      /* SYN, ABS */
      .ev = { 0x09 },
      /* X only */
      .abs = { 0x01 },
      .hid_report_descriptor_length = sizeof (fanatec_handbrake_hid_report_descriptor),
      .hid_report_descriptor = &fanatec_handbrake_hid_report_descriptor[0],
    },
    { /* Artificial test data, not a real device */
      .name = "Fake accelerometer with fewer than usual axes reported",
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER,
      /* SYN, ABS */
      .ev = { 0x09 },
      /* X only */
      .abs = { 0x01 },
      /* ACCELEROMETER */
      .props = { 0x40 },
    },
    { /* Artificial test data, not a real device */
      .name = "Fake pointing stick with no buttons",
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_POINTING_STICK,
      /* SYN, REL */
      .ev = { 0x05 },
      /* X,Y */
      .rel = { 0x03 },
      /* POINTER, POINTING_STICK */
      .props = { 0x21 },
    },
    { /* Artificial test data, not a real device */
      .name = "Fake buttonpad",
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHPAD,
      /* SYN, ABS */
      .ev = { 0x09 },
      /* X,Y */
      .abs = { 0x03 },
      /* POINTER, BUTTONPAD */
      .props = { 0x05 },
    },
    {
      .name = "No information",
      .expected = SRT_INPUT_DEVICE_TYPE_FLAGS_NONE,
    }
};

static void
test_input_device_guess (Fixture *f,
                         gconstpointer context)
{
  size_t i;

  for (i = 0; i < G_N_ELEMENTS (guess_tests); i++)
    {
      const GuessTest *t = &guess_tests[i];
      SrtEvdevCapabilities caps;
      size_t j;
      SrtInputDeviceTypeFlags actual;
      g_autofree gchar *expected_str = NULL;
      g_autofree gchar *actual_str = NULL;

      g_test_message ("%s", t->name);

      /* The real SrtEvdevCapabilities rounds the sizes up to the next
       * 32- or 64-bit boundary, and GuessTest rounds them up to the next
       * 8-bit boundary, so GuessTest is the same size or smaller. */
      g_assert_cmpuint (sizeof (caps.ev), >=, sizeof (t->ev));
      g_assert_cmpuint (sizeof (caps.keys), >=, sizeof (t->keys));
      g_assert_cmpuint (sizeof (caps.abs), >=, sizeof (t->abs));
      g_assert_cmpuint (sizeof (caps.rel), >=, sizeof (t->rel));
      g_assert_cmpuint (sizeof (caps.ff), >=, sizeof (t->ff));
      g_assert_cmpuint (sizeof (caps.props), >=, sizeof (t->props));

      memset (&caps, '\0', sizeof (caps));
      memcpy (caps.ev, t->ev, sizeof (t->ev));
      memcpy (caps.keys, t->keys, sizeof (t->keys));
      memcpy (caps.abs, t->abs, sizeof (t->abs));
      memcpy (caps.rel, t->rel, sizeof (t->rel));
      memcpy (caps.ff, t->ff, sizeof (t->ff));
      memcpy (caps.props, t->props, sizeof (t->props));

      for (j = 0; j < G_N_ELEMENTS (caps.ev); j++)
        caps.ev[j] = GULONG_FROM_LE (caps.ev[j]);

      for (j = 0; j < G_N_ELEMENTS (caps.keys); j++)
        caps.keys[j] = GULONG_FROM_LE (caps.keys[j]);

      for (j = 0; j < G_N_ELEMENTS (caps.abs); j++)
        caps.abs[j] = GULONG_FROM_LE (caps.abs[j]);

      for (j = 0; j < G_N_ELEMENTS (caps.rel); j++)
        caps.rel[j] = GULONG_FROM_LE (caps.rel[j]);

      for (j = 0; j < G_N_ELEMENTS (caps.ff); j++)
        caps.ff[j] = GULONG_FROM_LE (caps.ff[j]);

      for (j = 0; j < G_N_ELEMENTS (caps.props); j++)
        caps.props[j] = GULONG_FROM_LE (caps.props[j]);

      _srt_evdev_capabilities_dump (&caps);

      /* Now we can check whether our guess works */
      actual = _srt_evdev_capabilities_guess_type (&caps);

      expected_str = g_flags_to_string (SRT_TYPE_INPUT_DEVICE_TYPE_FLAGS,
                                        t->expected);
      g_test_message ("Expected: %s", expected_str);
      actual_str = g_flags_to_string (SRT_TYPE_INPUT_DEVICE_TYPE_FLAGS,
                                      actual);
      g_test_message ("Actual: %s", actual_str);

      if (t->todo == NULL)
        g_assert_cmphex (actual, ==, t->expected);
      else if (actual == t->expected)
        g_test_message ("Got expected result even though marked as TODO?");
      else
        g_test_message ("Ignoring known mismatch: %s", t->todo);
    }
}

static void
test_input_device_identity_from_hid_uevent (Fixture *f,
                                            gconstpointer context)
{
  static const char text[] =
    "DRIVER=hid-steam\n"
    "HID_ID=0003:000028DE:00001142\n"
    "HID_NAME=Valve Software Steam Controller\n"
    "HID_PHYS=usb-0000:00:14.0-1.1/input0\n"
    "HID_UNIQ=serialnumber\n"
    "MODALIAS=hid:b0003g0001v000028DEp00001142\n";
  guint32 bus_type, vendor_id, product_id;
  g_autofree gchar *name = NULL;
  g_autofree gchar *phys = NULL;
  g_autofree gchar *uniq = NULL;

  g_assert_true (_srt_get_identity_from_hid_uevent (text,
                                                    &bus_type,
                                                    &vendor_id,
                                                    &product_id,
                                                    &name,
                                                    &phys,
                                                    &uniq));
  g_assert_cmphex (bus_type, ==, 0x0003);
  g_assert_cmphex (vendor_id, ==, 0x28de);
  g_assert_cmphex (product_id, ==, 0x1142);
  g_assert_cmpstr (name, ==, "Valve Software Steam Controller");
  g_assert_cmpstr (phys, ==, "usb-0000:00:14.0-1.1/input0");
  /* Real Steam Controllers don't expose a serial number here, but it's
   * a better test if we include one */
  g_assert_cmpstr (uniq, ==, "serialnumber");
}

#define VENDOR_SONY 0x0268
#define PRODUCT_SONY_PS3 0x054c

/* These aren't in the real vendor/product IDs, but we add them here
 * to make the test able to distinguish. They look a bit like HID,
 * EVDE(v) and USB, if you squint. */
#define HID_MARKER 0x41D00000
#define EVDEV_MARKER 0xE7DE0000
#define USB_MARKER 0x05B00000

/* The test below assumes EV_MAX doesn't increase its value */
G_STATIC_ASSERT (EV_MAX <= 31);
/* Same for INPUT_PROP_MAX */
G_STATIC_ASSERT (INPUT_PROP_MAX <= 31);

static void
test_input_device_usb (Fixture *f,
                       gconstpointer context)
{
  g_autoptr(MockInputDevice) mock_device = mock_input_device_new ();
  SrtInputDevice *device = SRT_INPUT_DEVICE (mock_device);
  SrtSimpleInputDevice *simple = SRT_SIMPLE_INPUT_DEVICE (mock_device);
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);
  g_auto(GStrv) udev_properties = NULL;
  g_autofree gchar *uevent = NULL;
  g_autofree gchar *hid_uevent = NULL;
  g_autofree gchar *input_uevent = NULL;
  g_autofree gchar *usb_uevent = NULL;
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
  } identity = { 1, 1, 1, 1 };
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    const char *name;
    const char *phys;
    const char *uniq;
  } hid_identity = { 1, 1, 1, "x", "x", "x" };
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *name;
    const char *phys;
    const char *uniq;
  } input_identity = { 1, 1, 1, 1, "x", "x", "x" };
  struct
  {
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *manufacturer;
    const char *product;
    const char *serial;
  } usb_identity = { 1, 1, 1, "x", "x", "x" };
  unsigned long evbits;
  /* Initialize the first two to nonzero to check that they get zeroed */
  unsigned long bits[LONGS_FOR_BITS (HIGHEST_EVENT_CODE)] = { 0xa, 0xb };
  gsize i;

  simple->iface_flags = (SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT
                              | SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE);
  simple->dev_node = g_strdup ("/dev/input/event0");
  simple->sys_path = g_strdup ("/sys/devices/mock/usb/hid/input/input0/event0");
  simple->subsystem = g_strdup ("input");
  simple->udev_properties = g_new0 (gchar *, 2);
  simple->udev_properties[0] = g_strdup ("ID_INPUT_JOYSTICK=1");
  simple->udev_properties[1] = NULL;
  simple->uevent = g_strdup ("A=a\nB=b\n");
  /* This is a semi-realistic PS3 controller. */
  simple->type_flags = SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK;
  simple->bus_type = BUS_USB;
  simple->vendor_id = VENDOR_SONY;
  simple->product_id = PRODUCT_SONY_PS3;
  simple->version = 0x8111;

  /* We don't set all the bits, just enough to be vaguely realistic */
  set_bit (EV_KEY, simple->evdev_caps.ev);
  set_bit (EV_ABS, simple->evdev_caps.ev);
  set_bit (BTN_A, simple->evdev_caps.keys);
  set_bit (BTN_B, simple->evdev_caps.keys);
  set_bit (BTN_TL, simple->evdev_caps.keys);
  set_bit (BTN_TR, simple->evdev_caps.keys);
  set_bit (ABS_X, simple->evdev_caps.abs);
  set_bit (ABS_Y, simple->evdev_caps.abs);
  set_bit (ABS_RX, simple->evdev_caps.abs);
  set_bit (ABS_RY, simple->evdev_caps.abs);

  g_debug ("Mock device capabilities:");
  _srt_evdev_capabilities_dump (&simple->evdev_caps);

  simple->hid_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb/hid");
  simple->hid_ancestor.uevent = g_strdup ("HID=yes\n");
  /* The part in square brackets isn't present on the real device, but
   * makes this test more thorough by letting us distinguish. */
  simple->hid_ancestor.name = g_strdup ("Sony PLAYSTATION(R)3 Controller [hid]");
  simple->hid_ancestor.phys = g_strdup ("usb-0000:00:14.0-1/input0");
  simple->hid_ancestor.uniq = g_strdup ("12:34:56:78:9a:bc");
  simple->hid_ancestor.bus_type = HID_MARKER | BUS_USB;
  simple->hid_ancestor.vendor_id = HID_MARKER | VENDOR_SONY;
  simple->hid_ancestor.product_id = HID_MARKER | PRODUCT_SONY_PS3;

  simple->input_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb/hid/input");
  simple->input_ancestor.uevent = g_strdup ("INPUT=yes\n");
  simple->input_ancestor.name = g_strdup ("Sony PLAYSTATION(R)3 Controller [input]");
  simple->input_ancestor.phys = NULL;
  simple->input_ancestor.uniq = NULL;
  simple->input_ancestor.bus_type = EVDEV_MARKER | BUS_USB;
  simple->input_ancestor.vendor_id = EVDEV_MARKER | VENDOR_SONY;
  simple->input_ancestor.product_id = EVDEV_MARKER | PRODUCT_SONY_PS3;
  simple->input_ancestor.version = EVDEV_MARKER | 0x8111;

  simple->usb_device_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb");
  simple->usb_device_ancestor.uevent = g_strdup ("USB=usb_device\n");
  simple->usb_device_ancestor.vendor_id = USB_MARKER | VENDOR_SONY;
  simple->usb_device_ancestor.product_id = USB_MARKER | PRODUCT_SONY_PS3;
  simple->usb_device_ancestor.device_version = USB_MARKER | 0x0100;
  simple->usb_device_ancestor.manufacturer = g_strdup ("Sony");
  simple->usb_device_ancestor.product = g_strdup ("PLAYSTATION(R)3 Controller");
  simple->usb_device_ancestor.serial = NULL;

  g_assert_cmphex (srt_input_device_get_type_flags (device), ==,
                   SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK);
  g_assert_cmpuint (srt_input_device_get_interface_flags (device), ==,
                    SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT
                    | SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE);
  g_assert_cmpstr (srt_input_device_get_dev_node (device), ==,
                   "/dev/input/event0");
  g_assert_cmpstr (srt_input_device_get_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid/input/input0/event0");
  g_assert_cmpstr (srt_input_device_get_subsystem (device), ==, "input");

  uevent = srt_input_device_dup_uevent (device);
  g_assert_cmpstr (uevent, ==, "A=a\nB=b\n");

  g_assert_cmpstr (srt_input_device_get_hid_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid");
  hid_uevent = srt_input_device_dup_hid_uevent (device);
  g_assert_cmpstr (hid_uevent, ==, "HID=yes\n");

  g_assert_cmpstr (srt_input_device_get_input_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid/input");
  input_uevent = srt_input_device_dup_input_uevent (device);
  g_assert_cmpstr (input_uevent, ==, "INPUT=yes\n");

  g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (device), ==,
                   "/sys/devices/mock/usb");
  usb_uevent = srt_input_device_dup_usb_device_uevent (device);
  g_assert_cmpstr (usb_uevent, ==, "USB=usb_device\n");

  udev_properties = srt_input_device_dup_udev_properties (device);
  g_assert_nonnull (udev_properties);
  g_assert_cmpstr (udev_properties[0], ==, "ID_INPUT_JOYSTICK=1");
  g_assert_cmpstr (udev_properties[1], ==, NULL);

  g_assert_true (srt_input_device_get_identity (device,
                                                NULL, NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_identity (device,
                                                &identity.bus_type,
                                                &identity.vendor_id,
                                                &identity.product_id,
                                                &identity.version));
  g_assert_cmphex (identity.bus_type, ==, BUS_USB);
  g_assert_cmphex (identity.vendor_id, ==, VENDOR_SONY);
  g_assert_cmphex (identity.product_id, ==, PRODUCT_SONY_PS3);
  g_assert_cmphex (identity.version, ==, 0x8111);

  g_assert_true (srt_input_device_get_hid_identity (device,
                                                    NULL, NULL, NULL,
                                                    NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_hid_identity (device,
                                                    &hid_identity.bus_type,
                                                    &hid_identity.vendor_id,
                                                    &hid_identity.product_id,
                                                    &hid_identity.name,
                                                    &hid_identity.phys,
                                                    &hid_identity.uniq));
  g_assert_cmphex (hid_identity.bus_type, ==, HID_MARKER | BUS_USB);
  g_assert_cmphex (hid_identity.vendor_id, ==, HID_MARKER | VENDOR_SONY);
  g_assert_cmphex (hid_identity.product_id, ==, HID_MARKER | PRODUCT_SONY_PS3);
  g_assert_cmpstr (hid_identity.name, ==, "Sony PLAYSTATION(R)3 Controller [hid]");
  g_assert_cmpstr (hid_identity.phys, ==, "usb-0000:00:14.0-1/input0");
  g_assert_cmpstr (hid_identity.uniq, ==, "12:34:56:78:9a:bc");

  g_assert_true (srt_input_device_get_input_identity (device,
                                                      NULL, NULL, NULL, NULL,
                                                      NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_input_identity (device,
                                                      &input_identity.bus_type,
                                                      &input_identity.vendor_id,
                                                      &input_identity.product_id,
                                                      &input_identity.version,
                                                      &input_identity.name,
                                                      &input_identity.phys,
                                                      &input_identity.uniq));
  g_assert_cmphex (input_identity.bus_type, ==, EVDEV_MARKER | BUS_USB);
  g_assert_cmphex (input_identity.vendor_id, ==, EVDEV_MARKER | VENDOR_SONY);
  g_assert_cmphex (input_identity.product_id, ==, EVDEV_MARKER | PRODUCT_SONY_PS3);
  g_assert_cmphex (input_identity.version, ==, EVDEV_MARKER | 0x8111);
  g_assert_cmpstr (input_identity.name, ==, "Sony PLAYSTATION(R)3 Controller [input]");
  g_assert_cmpstr (input_identity.phys, ==, NULL);
  g_assert_cmpstr (input_identity.uniq, ==, NULL);

  g_assert_true (srt_input_device_get_usb_device_identity (device,
                                                           NULL, NULL, NULL,
                                                           NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_usb_device_identity (device,
                                                           &usb_identity.vendor_id,
                                                           &usb_identity.product_id,
                                                           &usb_identity.version,
                                                           &usb_identity.manufacturer,
                                                           &usb_identity.product,
                                                           &usb_identity.serial));
  g_assert_cmphex (usb_identity.vendor_id, ==, USB_MARKER | VENDOR_SONY);
  g_assert_cmphex (usb_identity.product_id, ==, USB_MARKER | PRODUCT_SONY_PS3);
  g_assert_cmpstr (usb_identity.manufacturer, ==, "Sony");
  g_assert_cmpstr (usb_identity.product, ==, "PLAYSTATION(R)3 Controller");
  g_assert_cmpstr (usb_identity.serial, ==, NULL);

  g_debug ("Capabilities internally:");
  _srt_evdev_capabilities_dump (iface->peek_event_capabilities (device));

  /* This assumes EV_MAX doesn't increase its value */
  g_assert_cmpuint (srt_input_device_get_event_types (device, NULL, 0),
                    ==, 1);
  g_assert_cmpuint (srt_input_device_get_event_types (device, &evbits, 1),
                    ==, 1);
  g_assert_cmphex (evbits, ==, simple->evdev_caps.ev[0]);
  g_assert_cmphex (evbits & (1 << EV_KEY), ==, 1 << EV_KEY);
  g_assert_cmphex (evbits & (1 << EV_ABS), ==, 1 << EV_ABS);
  g_assert_cmphex (evbits & (1 << EV_SW), ==, 0);
  g_assert_cmphex (evbits & (1 << EV_MSC), ==, 0);
  g_assert_cmpint (srt_input_device_has_event_type (device, EV_KEY), ==, TRUE);
  g_assert_cmpint (srt_input_device_has_event_type (device, EV_SW), ==, FALSE);
  g_assert_cmpint (srt_input_device_has_event_capability (device, 0, EV_KEY),
                   ==, TRUE);
  g_assert_cmpint (srt_input_device_has_event_capability (device, 0, EV_SW),
                   ==, FALSE);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, 0,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    ==, 1);
  g_assert_cmphex (bits[0], ==, evbits);

  for (i = 1; i < G_N_ELEMENTS (bits); i++)
    g_assert_cmphex (bits[i], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_KEY,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >, 1);
  /* Low KEY_ codes are keyboard keys, which we don't have */
  g_assert_cmphex (bits[0], ==, 0);
  g_assert_cmpint (test_bit (BTN_A, bits), ==, 1);
  g_assert_cmpint (test_bit (BTN_STYLUS, bits), ==, 0);
  g_assert_cmpint (test_bit (KEY_SEMICOLON, bits), ==, 0);
  g_assert_cmpmem (bits,
                   MIN (sizeof (bits), sizeof (simple->evdev_caps.keys)),
                   simple->evdev_caps.keys,
                   MIN (sizeof (bits), sizeof (simple->evdev_caps.keys)));

  /* ABS axes also match */
  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_ABS,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmpint (test_bit (ABS_X, bits), ==, 1);
  g_assert_cmpint (test_bit (ABS_Z, bits), ==, 0);
  g_assert_cmpmem (bits,
                   MIN (sizeof (bits), sizeof (simple->evdev_caps.abs)),
                   simple->evdev_caps.abs,
                   MIN (sizeof (bits), sizeof (simple->evdev_caps.abs)));

  /* REL axes also match (in fact we don't have any, but we still store
   * the bitfield) */
  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_REL,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmpmem (bits,
                   MIN (sizeof (bits), sizeof (simple->evdev_caps.rel)),
                   simple->evdev_caps.rel,
                   MIN (sizeof (bits), sizeof (simple->evdev_caps.rel)));

  /* We don't support EV_SW */
  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_SW,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    ==, 0);

  for (i = 1; i < G_N_ELEMENTS (bits); i++)
    g_assert_cmphex (bits[i], ==, 0);

  g_assert_cmpuint (srt_input_device_get_input_properties (device,
                                                           bits,
                                                           G_N_ELEMENTS (bits)),
                    ==, 1);
  g_assert_cmphex (bits[0], ==, 0);
  g_assert_cmpint (srt_input_device_has_input_property (device, INPUT_PROP_SEMI_MT),
                   ==, FALSE);

  for (i = 1; i < G_N_ELEMENTS (bits); i++)
    g_assert_cmphex (bits[i], ==, 0);
}

static gboolean
in_monitor_main_context (Fixture *f)
{
  if (f->monitor_context == NULL)
    return g_main_context_is_owner (g_main_context_default ());

  return g_main_context_is_owner (f->monitor_context);
}

static void
device_added_cb (SrtInputDeviceMonitor *monitor,
                 SrtInputDevice *device,
                 gpointer user_data)
{
  Fixture *f = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *message = NULL;
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
  } identity = { 1, 1, 1, 1 };
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    const char *name;
    const char *phys;
    const char *uniq;
  } hid_identity = { 1, 1, 1, "x", "x", "x" };
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *name;
    const char *phys;
    const char *uniq;
  } input_identity = { 1, 1, 1, 1, "x", "x", "x" };
  struct
  {
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *manufacturer;
    const char *product;
    const char *serial;
  } usb_identity = { 1, 1, 1, "x", "x", "x" };
  SrtInputDeviceInterfaceFlags iface_flags;
  int fd;

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));
  g_assert_true (SRT_IS_INPUT_DEVICE (device));

  message = g_strdup_printf ("added device: %s",
                             srt_input_device_get_dev_node (device));
  g_debug ("%s: %s", G_OBJECT_TYPE_NAME (monitor), message);

  iface_flags = srt_input_device_get_interface_flags (device);

  if (srt_input_device_get_identity (device,
                                     &identity.bus_type,
                                     &identity.vendor_id,
                                     &identity.product_id,
                                     &identity.version))
    {
      g_assert_true (srt_input_device_get_identity (device,
                                                    NULL, NULL, NULL, NULL));
    }
  else
    {
      g_assert_false (srt_input_device_get_identity (device,
                                                     NULL, NULL, NULL, NULL));
      /* previous contents are untouched */
      g_assert_cmphex (identity.bus_type, ==, 1);
      g_assert_cmphex (identity.vendor_id, ==, 1);
      g_assert_cmphex (identity.product_id, ==, 1);
      g_assert_cmphex (identity.version, ==, 1);
    }

  if (srt_input_device_get_hid_identity (device,
                                         &hid_identity.bus_type,
                                         &hid_identity.vendor_id,
                                         &hid_identity.product_id,
                                         &hid_identity.name,
                                         &hid_identity.phys,
                                         &hid_identity.uniq))
    {
      g_assert_true (srt_input_device_get_hid_identity (device,
                                                        NULL, NULL, NULL,
                                                        NULL, NULL, NULL));
    }
  else
    {
      g_assert_false (srt_input_device_get_hid_identity (device,
                                                         NULL, NULL, NULL,
                                                         NULL, NULL, NULL));
      /* previous contents are untouched */
      g_assert_cmphex (hid_identity.bus_type, ==, 1);
      g_assert_cmphex (hid_identity.vendor_id, ==, 1);
      g_assert_cmphex (hid_identity.product_id, ==, 1);
      g_assert_cmpstr (hid_identity.name, ==, "x");
      g_assert_cmpstr (hid_identity.phys, ==, "x");
      g_assert_cmpstr (hid_identity.uniq, ==, "x");
    }

  if (srt_input_device_get_input_identity (device,
                                           &input_identity.bus_type,
                                           &input_identity.vendor_id,
                                           &input_identity.product_id,
                                           &input_identity.version,
                                           &input_identity.name,
                                           &input_identity.phys,
                                           &input_identity.uniq))
    {
      g_assert_true (srt_input_device_get_input_identity (device,
                                                          NULL, NULL, NULL, NULL,
                                                          NULL, NULL, NULL));
    }
  else
    {
      g_assert_false (srt_input_device_get_input_identity (device,
                                                           NULL, NULL, NULL, NULL,
                                                           NULL, NULL, NULL));
      /* previous contents are untouched */
      g_assert_cmphex (input_identity.bus_type, ==, 1);
      g_assert_cmphex (input_identity.vendor_id, ==, 1);
      g_assert_cmphex (input_identity.product_id, ==, 1);
      g_assert_cmphex (input_identity.version, ==, 1);
      g_assert_cmpstr (input_identity.name, ==, "x");
      g_assert_cmpstr (input_identity.phys, ==, "x");
      g_assert_cmpstr (input_identity.uniq, ==, "x");
    }

  if (srt_input_device_get_usb_device_identity (device,
                                                &usb_identity.vendor_id,
                                                &usb_identity.product_id,
                                                &usb_identity.version,
                                                &usb_identity.manufacturer,
                                                &usb_identity.product,
                                                &usb_identity.serial))
    {
      g_assert_true (srt_input_device_get_usb_device_identity (device,
                                                               NULL, NULL, NULL,
                                                               NULL, NULL, NULL));
    }
  else
    {
      g_assert_false (srt_input_device_get_usb_device_identity (device,
                                                                NULL, NULL, NULL,
                                                                NULL, NULL, NULL));
      /* previous contents are untouched */
      g_assert_cmphex (usb_identity.vendor_id, ==, 1);
      g_assert_cmphex (usb_identity.product_id, ==, 1);
      g_assert_cmphex (usb_identity.version, ==, 1);
      g_assert_cmpstr (usb_identity.manufacturer, ==, "x");
      g_assert_cmpstr (usb_identity.product, ==, "x");
      g_assert_cmpstr (usb_identity.serial, ==, "x");
    }

  fd = srt_input_device_open (device, O_RDONLY | O_NONBLOCK, &error);

  if (iface_flags & SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE)
    {
      g_assert_no_error (error);
      g_assert_cmpint (fd, >=, 0);
    }
  else
    {
      g_assert_nonnull (error);
      g_assert_cmpint (fd, <, 0);
    }

  glnx_close_fd (&fd);
  g_clear_error (&error);

  fd = srt_input_device_open (device, O_RDWR | O_NONBLOCK, &error);

  if (iface_flags & SRT_INPUT_DEVICE_INTERFACE_FLAGS_READ_WRITE)
    {
      g_assert_no_error (error);
      g_assert_cmpint (fd, >=, 0);
    }
  else
    {
      g_assert_nonnull (error);
      g_assert_cmpint (fd, <, 0);
    }

  glnx_close_fd (&fd);
  g_clear_error (&error);

  /* Unsupported flags (currently everything except O_NONBLOCK) are
   * not allowed */
  fd = srt_input_device_open (device, O_RDONLY | O_SYNC, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  glnx_close_fd (&fd);
  g_clear_error (&error);

  /* For the mock device monitor, we know exactly what to expect, so
   * we can compare the expected log with what actually happened. For
   * real device monitors, we don't know what's physically present,
   * so we have to just emit debug messages. */
  if (f->config->type == MOCK)
    {
      g_autofree gchar *uevent = NULL;
      g_autofree gchar *hid_uevent = NULL;
      g_autofree gchar *input_uevent = NULL;
      g_autofree gchar *usb_uevent = NULL;
      g_auto(GStrv) udev_properties = NULL;
      unsigned long evbits;
      unsigned long bits[LONGS_FOR_BITS (HIGHEST_EVENT_CODE)];
      gsize i;

      g_assert_cmphex (srt_input_device_get_type_flags (device), ==,
                       SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK);

      g_assert_cmphex (identity.bus_type, ==, BUS_USB);
      g_assert_cmphex (identity.vendor_id, ==, VENDOR_VALVE);
      g_assert_cmphex (identity.product_id, ==, PRODUCT_VALVE_STEAM_CONTROLLER);
      g_assert_cmphex (identity.version, ==, 0x0111);

      g_assert_cmphex (hid_identity.bus_type, ==, HID_MARKER | BUS_USB);
      g_assert_cmphex (hid_identity.vendor_id, ==, HID_MARKER | VENDOR_VALVE);
      g_assert_cmphex (hid_identity.product_id, ==, HID_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER);
      g_assert_cmpstr (hid_identity.name, ==, "Valve Software Steam Controller");
      g_assert_cmpstr (hid_identity.phys, ==, "[hid]usb-0000:00:14.0-1.2/input1");
      g_assert_cmpstr (hid_identity.uniq, ==, "");

      g_assert_cmphex (input_identity.bus_type, ==, EVDEV_MARKER | BUS_USB);
      g_assert_cmphex (input_identity.vendor_id, ==, EVDEV_MARKER | VENDOR_VALVE);
      g_assert_cmphex (input_identity.product_id, ==, EVDEV_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER);
      g_assert_cmphex (input_identity.version, ==, EVDEV_MARKER | 0x0111);
      g_assert_cmpstr (input_identity.name, ==, "Wireless Steam Controller");
      g_assert_cmpstr (input_identity.phys, ==, "[input]usb-0000:00:14.0-1.2/input1");
      g_assert_cmpstr (input_identity.uniq, ==, "12345678");

      g_assert_cmphex (usb_identity.vendor_id, ==, USB_MARKER | VENDOR_VALVE);
      g_assert_cmphex (usb_identity.product_id, ==, USB_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER);
      g_assert_cmphex (usb_identity.version, ==, USB_MARKER | 0x0001);
      g_assert_cmpstr (usb_identity.manufacturer, ==, "Valve Software");
      g_assert_cmpstr (usb_identity.product, ==, "Steam Controller");
      g_assert_cmpstr (usb_identity.serial, ==, NULL);

      uevent = srt_input_device_dup_uevent (device);
      g_assert_cmpstr (uevent, ==, "ONE=1\nTWO=2\n");

      udev_properties = srt_input_device_dup_udev_properties (device);
      g_assert_nonnull (udev_properties);
      g_assert_cmpstr (udev_properties[0], ==, "ID_INPUT_JOYSTICK=1");
      g_assert_cmpstr (udev_properties[1], ==, NULL);

      g_assert_cmpstr (srt_input_device_get_hid_sys_path (device), ==,
                       "/sys/devices/mock/usb/hid");
      hid_uevent = srt_input_device_dup_hid_uevent (device);
      g_assert_cmpstr (hid_uevent, ==, "HID=yes\n");

      g_assert_cmpstr (srt_input_device_get_input_sys_path (device), ==,
                       "/sys/devices/mock/usb/hid/input");
      input_uevent = srt_input_device_dup_input_uevent (device);
      g_assert_cmpstr (input_uevent, ==, "INPUT=yes\n");

      g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (device), ==,
                       "/sys/devices/mock/usb");
      usb_uevent = srt_input_device_dup_usb_device_uevent (device);
      g_assert_cmpstr (usb_uevent, ==, "USB=usb_device\n");

      /* This assumes EV_MAX doesn't increase its value */
      g_assert_cmpuint (srt_input_device_get_event_types (device, NULL, 0),
                        ==, 1);
      g_assert_cmpuint (srt_input_device_get_event_types (device, &evbits, 1),
                        ==, 1);
      g_assert_cmphex (evbits & (1 << EV_KEY), ==, 1 << EV_KEY);
      g_assert_cmphex (evbits & (1 << EV_ABS), ==, 1 << EV_ABS);
      g_assert_cmphex (evbits & (1 << EV_SW), ==, 0);
      g_assert_cmphex (evbits & (1 << EV_MSC), ==, 0);
      g_assert_cmpint (srt_input_device_has_event_type (device, EV_KEY), ==, TRUE);
      g_assert_cmpint (srt_input_device_has_event_type (device, EV_SW), ==, FALSE);
      g_assert_cmpint (srt_input_device_has_event_capability (device, 0, EV_KEY),
                       ==, TRUE);
      g_assert_cmpint (srt_input_device_has_event_capability (device, 0, EV_SW),
                       ==, FALSE);

      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, 0,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        ==, 1);
      g_assert_cmphex (bits[0], ==, evbits);

      for (i = 1; i < G_N_ELEMENTS (bits); i++)
        g_assert_cmphex (bits[i], ==, 0);

      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_KEY,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        >, 1);
      /* Low KEY_ codes are keyboard keys, which we don't have */
      g_assert_cmphex (bits[0], ==, 0);
      g_assert_cmpint (test_bit (BTN_A, bits), ==, 1);
      g_assert_cmpint (test_bit (BTN_STYLUS, bits), ==, 0);
      g_assert_cmpint (test_bit (KEY_SEMICOLON, bits), ==, 0);

      /* ABS axes also match */
      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_ABS,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        >=, 1);
      g_assert_cmpint (test_bit (ABS_X, bits), ==, 1);
      g_assert_cmpint (test_bit (ABS_Z, bits), ==, 0);

      /* REL axes also match (in fact we don't have any, but we still store
       * the bitfield) */
      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_REL,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        >=, 1);

      for (i = 1; i < G_N_ELEMENTS (bits); i++)
        g_assert_cmphex (bits[i], ==, 0);

      /* We don't support EV_SW */
      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_SW,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        ==, 0);

      for (i = 1; i < G_N_ELEMENTS (bits); i++)
        g_assert_cmphex (bits[i], ==, 0);

      g_assert_cmpuint (srt_input_device_get_input_properties (device,
                                                               bits,
                                                               G_N_ELEMENTS (bits)),
                        ==, 1);
      /* The mock implementation unrealistically sets INPUT_PROP_POINTER,
       * just so we have something nonzero to test against */
      g_assert_cmphex (bits[0], ==, (1 << INPUT_PROP_POINTER));
      g_assert_cmpint (srt_input_device_has_input_property (device, INPUT_PROP_POINTER),
                       ==, TRUE);
      g_assert_cmpint (srt_input_device_has_input_property (device, INPUT_PROP_SEMI_MT),
                       ==, FALSE);

      for (i = 1; i < G_N_ELEMENTS (bits); i++)
        g_assert_cmphex (bits[i], ==, 0);

      g_ptr_array_add (f->log, g_steal_pointer (&message));
    }

  g_assert_true (in_monitor_main_context (f));
}

static void
device_removed_cb (SrtInputDeviceMonitor *monitor,
                   SrtInputDevice *device,
                   gpointer user_data)
{
  Fixture *f = user_data;
  g_autofree gchar *message = NULL;

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));
  g_assert_true (SRT_IS_INPUT_DEVICE (device));

  message = g_strdup_printf ("removed device: %s",
                             srt_input_device_get_dev_node (device));
  g_debug ("%s: %s", G_OBJECT_TYPE_NAME (monitor), message);

  if (f->config->type == MOCK)
    g_ptr_array_add (f->log, g_steal_pointer (&message));

  g_assert_true (in_monitor_main_context (f));
}

static void
all_for_now_cb (SrtInputDeviceMonitor *monitor,
                gpointer user_data)
{
  Fixture *f = user_data;

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));

  g_ptr_array_add (f->log, g_strdup ("all for now"));
  g_debug ("%s: %s",
           G_OBJECT_TYPE_NAME (monitor),
           (const char *) g_ptr_array_index (f->log, f->log->len - 1));

  g_assert_true (in_monitor_main_context (f));
}

static gboolean
done_cb (gpointer user_data)
{
  gboolean *done = user_data;

  *done = TRUE;
  return G_SOURCE_REMOVE;
}

/* This is the equivalent of g_idle_add() for a non-default main context */
static guint
idle_add_in_context (GSourceFunc function,
                     gpointer data,
                     GMainContext *context)
{
  g_autoptr(GSource) idler = g_idle_source_new ();

  g_source_set_callback (idler, function, data, NULL);
  return g_source_attach (idler, context);
}

static SrtInputDeviceMonitor *
input_device_monitor_new (Fixture *f,
                          SrtInputDeviceMonitorFlags flags)
{
  switch (f->config->type)
    {
      case DIRECT:
        flags |= SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT;
        return srt_input_device_monitor_new (flags);

      case UDEV:
        flags |= SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV;
        return srt_input_device_monitor_new (flags);

      case MOCK:
      default:
        return SRT_INPUT_DEVICE_MONITOR (mock_input_device_monitor_new (flags));
    }
}

/*
 * Test the basic behaviour of an input device monitor:
 * - start
 * - do initial enumeration
 * - watch for new devices
 * - emit signals in the correct main context
 * - stop
 */
static void
test_input_device_monitor (Fixture *f,
                           gconstpointer context)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok;
  gboolean did_default_idle = FALSE;
  gboolean did_context_idle = FALSE;
  guint i;

  if (f->skipped)
    return;

  f->monitor_context = g_main_context_new ();

  /* To check that the signals get emitted in the correct main-context,
   * temporarily set a new thread-default main-context while we create
   * the monitor. */
  g_main_context_push_thread_default (f->monitor_context);
    {
      monitor = input_device_monitor_new (f, SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE);
    }
  g_main_context_pop_thread_default (f->monitor_context);

  g_assert_nonnull (monitor);

  srt_input_device_monitor_request_evdev (monitor);
  srt_input_device_monitor_request_raw_hid (monitor);

  g_signal_connect (monitor, "added", G_CALLBACK (device_added_cb), f);
  g_signal_connect (monitor, "removed", G_CALLBACK (device_removed_cb), f);
  g_signal_connect (monitor, "all-for-now", G_CALLBACK (all_for_now_cb), f);

  /* Note that the signals are emitted in the main-context that was
   * thread-default at the time we created the object, not the
   * main-context that called start(). */
  ok = srt_input_device_monitor_start (monitor, &error);
  g_debug ("start() returned");
  g_assert_no_error (error);
  g_assert_true (ok);
  g_ptr_array_add (f->log, g_strdup ("start() returned"));

  g_idle_add (done_cb, &did_default_idle);
  idle_add_in_context (done_cb, &did_context_idle, f->monitor_context);

  i = 0;

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "start() returned");
  /* There's nothing else in the log yet */
  g_assert_cmpuint (f->log->len, ==, i);

  /* Iterating the default main context does not deliver signals */
  while (!did_default_idle)
    g_main_context_iteration (NULL, TRUE);

  g_assert_cmpuint (f->log->len, ==, i);

  /* Iterating the main context that was thread-default at the time we
   * constructed the monitor *does* deliver signals */
  while (!did_context_idle)
    g_main_context_iteration (f->monitor_context, TRUE);

  /* For the mock device monitor, we can predict which devices will be added,
   * so we log them and assert about them. For real device monitors we
   * can't reliably do this. */
  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event0");
    }

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "all for now");

  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event-connected-briefly");
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "removed device: /dev/input/event-connected-briefly");
    }

  g_assert_cmpuint (f->log->len, ==, i);

  /* Explicitly stop it here. We test not explicitly stopping in the
   * other test-case */
  srt_input_device_monitor_stop (monitor);

  /* It's possible that not all the memory used is freed until we have
   * iterated the main-context one last time */
  did_context_idle = FALSE;
  idle_add_in_context (done_cb, &did_context_idle, f->monitor_context);

  while (!did_context_idle)
    g_main_context_iteration (f->monitor_context, TRUE);
}

/*
 * Test things we couldn't test in the previous test-case:
 * - the ONCE flag, which disables monitoring
 * - using our thread-default main-context throughout
 */
static void
test_input_device_monitor_once (Fixture *f,
                                gconstpointer context)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok;
  gboolean done = FALSE;
  guint i;

  if (f->skipped)
    return;

  monitor = input_device_monitor_new (f, SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE);
  g_assert_nonnull (monitor);

  srt_input_device_monitor_request_evdev (monitor);
  srt_input_device_monitor_request_raw_hid (monitor);

  g_signal_connect (monitor, "added", G_CALLBACK (device_added_cb), f);
  g_signal_connect (monitor, "removed", G_CALLBACK (device_removed_cb), f);
  g_signal_connect (monitor, "all-for-now", G_CALLBACK (all_for_now_cb), f);

  ok = srt_input_device_monitor_start (monitor, &error);
  g_debug ("start() returned");
  g_assert_no_error (error);
  g_assert_true (ok);
  g_ptr_array_add (f->log, g_strdup ("start() returned"));

  g_idle_add (done_cb, &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  i = 0;

  /* Because the same main context was the thread-default at the
   * time we created the object and at the time we called start(),
   * the first batch of signals arrive even before start() has returned. */
  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event0");
    }

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "all for now");
  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "start() returned");
  g_assert_cmpuint (f->log->len, ==, i);

  /* Don't explicitly stop it here. We test explicitly stopping in the
   * other test-case */
  g_clear_object (&monitor);

  /* It's possible that not all the memory used is freed until we have
   * iterate the main-context one last time */
  done = FALSE;
  g_idle_add (done_cb, &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];
  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/input-device/from-json", Fixture, NULL,
              setup, test_input_device_from_json, teardown);
  g_test_add ("/input-device/guess", Fixture, NULL,
              setup, test_input_device_guess, teardown);
  g_test_add ("/input-device/identity-from-hid-uevent", Fixture, NULL,
              setup, test_input_device_identity_from_hid_uevent, teardown);
  g_test_add ("/input-device/usb", Fixture, NULL,
              setup, test_input_device_usb, teardown);
  g_test_add ("/input-device/monitor/mock", Fixture, NULL,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/mock", Fixture, NULL,
              setup, test_input_device_monitor_once, teardown);
  g_test_add ("/input-device/monitor/direct", Fixture, &direct_config,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/direct", Fixture, &direct_config,
              setup, test_input_device_monitor_once, teardown);
  g_test_add ("/input-device/monitor/udev", Fixture, &udev_config,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/udev", Fixture, &udev_config,
              setup, test_input_device_monitor_once, teardown);

  return g_test_run ();
}
