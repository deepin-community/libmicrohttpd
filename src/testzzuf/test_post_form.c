/*
     This file is part of libmicrohttpd
     Copyright (C) 2007, 2008 Christian Grothoff

     libmicrohttpd is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     libmicrohttpd is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with libmicrohttpd; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
     Boston, MA 02110-1301, USA.
*/

/**
 * @file test_post_form.c
 * @brief  Testcase for libmicrohttpd POST operations using multipart/postform data
 * @author Christian Grothoff
 */

#include "MHD_config.h"
#include "platform.h"
#include <curl/curl.h>
#include <microhttpd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef WINDOWS
#include <unistd.h>
#endif


#include "socat.c"

static int oneone;

struct CBC
{
  char *buf;
  size_t pos;
  size_t size;
};


static void
completed_cb (void *cls,
              struct MHD_Connection *connection,
              void **con_cls,
              enum MHD_RequestTerminationCode toe)
{
  struct MHD_PostProcessor *pp = *con_cls;
  (void) cls; (void) connection; (void) toe;            /* Unused. Silent compiler warning. */

  if (NULL != pp)
    MHD_destroy_post_processor (pp);
  *con_cls = NULL;
}


static size_t
copyBuffer (void *ptr, size_t size, size_t nmemb, void *ctx)
{
  struct CBC *cbc = ctx;

  if (cbc->pos + size * nmemb > cbc->size)
    return 0;                   /* overflow */
  memcpy (&cbc->buf[cbc->pos], ptr, size * nmemb);
  cbc->pos += size * nmemb;
  return size * nmemb;
}


/**
 * Note that this post_iterator is not perfect
 * in that it fails to support incremental processing.
 * (to be fixed in the future)
 */
static enum MHD_Result
post_iterator (void *cls,
               enum MHD_ValueKind kind,
               const char *key,
               const char *filename,
               const char *content_type,
               const char *transfer_encoding,
               const char *value, uint64_t off, size_t size)
{
  int *eok = cls;
  (void) kind; (void) filename; (void) content_type; /* Unused. Silent compiler warning. */
  (void) transfer_encoding; (void) off;            /* Unused. Silent compiler warning. */

  if (key == NULL)
    return MHD_YES;
#if 0
  fprintf (stderr, "PI sees %s-%.*s\n", key, size, value);
#endif
  if ((0 == strcmp (key, "name")) &&
      (size == strlen ("daniel")) && (0 == strncmp (value, "daniel", size)))
    (*eok) |= 1;
  if ((0 == strcmp (key, "project")) &&
      (size == strlen ("curl")) && (0 == strncmp (value, "curl", size)))
    (*eok) |= 2;
  return MHD_YES;
}


static enum MHD_Result
ahc_echo (void *cls,
          struct MHD_Connection *connection,
          const char *url,
          const char *method,
          const char *version,
          const char *upload_data, size_t *upload_data_size,
          void **unused)
{
  static int eok;
  struct MHD_Response *response;
  struct MHD_PostProcessor *pp;
  enum MHD_Result ret;
  (void) cls; (void) version;      /* Unused. Silent compiler warning. */

  if (NULL == url)
    fprintf (stderr, "The \"url\" parameter is NULL.\n");
  if (NULL == method)
    fprintf (stderr, "The \"method\" parameter is NULL.\n");
  if (NULL == version)
    fprintf (stderr, "The \"version\" parameter is NULL.\n");
  if (NULL == upload_data_size)
    fprintf (stderr, "The \"upload_data_size\" parameter is NULL.\n");
  if ((0 != *upload_data_size) && (NULL == upload_data))
    fprintf (stderr, "Upload data is NULL with non-zero size.\n");
  if (0 != strcmp ("POST", method))
  {
    return MHD_NO;              /* unexpected method */
  }
  pp = *unused;
  if (pp == NULL)
  {
    eok = 0;
    pp = MHD_create_post_processor (connection, 1024, &post_iterator, &eok);
    if (pp == NULL)
      return MHD_NO;
    *unused = pp;
  }
  MHD_post_process (pp, upload_data, *upload_data_size);
  if ((eok == 3) && (0 == *upload_data_size))
  {
    response = MHD_create_response_from_buffer (strlen (url),
                                                (void *) url,
                                                MHD_RESPMEM_MUST_COPY);
    ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
    MHD_destroy_response (response);
    MHD_destroy_post_processor (pp);
    *unused = NULL;
    return ret;
  }
  *upload_data_size = 0;
  return MHD_YES;
}


static struct curl_httppost *
make_form ()
{
  struct curl_httppost *post = NULL;
  struct curl_httppost *last = NULL;

  curl_formadd (&post, &last, CURLFORM_COPYNAME, "name",
                CURLFORM_COPYCONTENTS, "daniel", CURLFORM_END);
  curl_formadd (&post, &last, CURLFORM_COPYNAME, "project",
                CURLFORM_COPYCONTENTS, "curl", CURLFORM_END);
  return post;
}


static int
testInternalPost ()
{
  struct MHD_Daemon *d;
  CURL *c;
  char buf[2048];
  struct CBC cbc;
  int i;
  struct curl_httppost *pd;

  cbc.buf = buf;
  cbc.size = 2048;
  cbc.pos = 0;
  d = MHD_start_daemon (
    MHD_USE_INTERNAL_POLLING_THREAD /* | MHD_USE_ERROR_LOG */,
    11080, NULL, NULL, &ahc_echo, NULL,
    MHD_OPTION_NOTIFY_COMPLETED, &completed_cb, NULL,
    MHD_OPTION_END);
  if (d == NULL)
    return 1;
  zzuf_socat_start ();
  for (i = 0; i < LOOP_COUNT; i++)
  {
    fprintf (stderr, ".");
    c = curl_easy_init ();
    curl_easy_setopt (c, CURLOPT_URL, "http://127.0.0.1:11081/hello_world");
    curl_easy_setopt (c, CURLOPT_WRITEFUNCTION, &copyBuffer);
    curl_easy_setopt (c, CURLOPT_WRITEDATA, &cbc);
    pd = make_form ();
    curl_easy_setopt (c, CURLOPT_HTTPPOST, pd);
    curl_easy_setopt (c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt (c, CURLOPT_TIMEOUT_MS, CURL_TIMEOUT);
    if (oneone)
      curl_easy_setopt (c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    else
      curl_easy_setopt (c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
    curl_easy_setopt (c, CURLOPT_CONNECTTIMEOUT_MS, CURL_TIMEOUT);
    /* NOTE: use of CONNECTTIMEOUT without also
     *   setting NOSIGNAL results in really weird
     *   crashes on my system! */
    curl_easy_setopt (c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_perform (c);
    curl_easy_cleanup (c);
    curl_formfree (pd);
  }
  fprintf (stderr, "\n");
  zzuf_socat_stop ();
  MHD_stop_daemon (d);
  return 0;
}


static int
testMultithreadedPost ()
{
  struct MHD_Daemon *d;
  CURL *c;
  char buf[2048];
  struct CBC cbc;
  int i;
  struct curl_httppost *pd;

  cbc.buf = buf;
  cbc.size = 2048;
  cbc.pos = 0;
  d = MHD_start_daemon (MHD_USE_THREAD_PER_CONNECTION
                        | MHD_USE_INTERNAL_POLLING_THREAD /* | MHD_USE_ERROR_LOG */,
                        11080, NULL, NULL, &ahc_echo, NULL,
                        MHD_OPTION_NOTIFY_COMPLETED, &completed_cb, NULL,
                        MHD_OPTION_END);
  if (d == NULL)
    return 16;
  zzuf_socat_start ();
  for (i = 0; i < LOOP_COUNT; i++)
  {
    fprintf (stderr, ".");
    c = curl_easy_init ();
    curl_easy_setopt (c, CURLOPT_URL, "http://127.0.0.1:11081/hello_world");
    curl_easy_setopt (c, CURLOPT_WRITEFUNCTION, &copyBuffer);
    curl_easy_setopt (c, CURLOPT_WRITEDATA, &cbc);
    pd = make_form ();
    curl_easy_setopt (c, CURLOPT_HTTPPOST, pd);
    curl_easy_setopt (c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt (c, CURLOPT_TIMEOUT_MS, CURL_TIMEOUT);
    if (oneone)
      curl_easy_setopt (c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    else
      curl_easy_setopt (c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
    curl_easy_setopt (c, CURLOPT_CONNECTTIMEOUT_MS, CURL_TIMEOUT);
    /* NOTE: use of CONNECTTIMEOUT without also
     *   setting NOSIGNAL results in really weird
     *   crashes on my system! */
    curl_easy_setopt (c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_perform (c);
    curl_easy_cleanup (c);
    curl_formfree (pd);
  }
  fprintf (stderr, "\n");
  zzuf_socat_stop ();
  MHD_stop_daemon (d);
  return 0;
}


static int
testExternalPost ()
{
  struct MHD_Daemon *d;
  CURL *c;
  char buf[2048];
  struct CBC cbc;
  CURLM *multi;
  CURLMcode mret;
  fd_set rs;
  fd_set ws;
  fd_set es;
  int max;
  int running;
  time_t start;
  struct timeval tv;
  struct curl_httppost *pd;
  int i;

  multi = NULL;
  cbc.buf = buf;
  cbc.size = 2048;
  cbc.pos = 0;
  d = MHD_start_daemon (MHD_NO_FLAG /* | MHD_USE_ERROR_LOG */,
                        1082, NULL, NULL, &ahc_echo, NULL,
                        MHD_OPTION_NOTIFY_COMPLETED, &completed_cb, NULL,
                        MHD_OPTION_END);
  if (d == NULL)
    return 256;
  multi = curl_multi_init ();
  if (multi == NULL)
  {
    MHD_stop_daemon (d);
    return 512;
  }
  zzuf_socat_start ();
  for (i = 0; i < LOOP_COUNT; i++)
  {
    fprintf (stderr, ".");

    c = curl_easy_init ();
    curl_easy_setopt (c, CURLOPT_URL, "http://127.0.0.1:1082/hello_world");
    curl_easy_setopt (c, CURLOPT_WRITEFUNCTION, &copyBuffer);
    curl_easy_setopt (c, CURLOPT_WRITEDATA, &cbc);
    pd = make_form ();
    curl_easy_setopt (c, CURLOPT_HTTPPOST, pd);
    curl_easy_setopt (c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt (c, CURLOPT_TIMEOUT, 150L);
    if (oneone)
      curl_easy_setopt (c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    else
      curl_easy_setopt (c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
    curl_easy_setopt (c, CURLOPT_CONNECTTIMEOUT, 15L);
    /* NOTE: use of CONNECTTIMEOUT without also
     *   setting NOSIGNAL results in really weird
     *   crashes on my system! */
    curl_easy_setopt (c, CURLOPT_NOSIGNAL, 1L);

    mret = curl_multi_add_handle (multi, c);
    if (mret != CURLM_OK)
    {
      curl_multi_cleanup (multi);
      curl_formfree (pd);
      curl_easy_cleanup (c);
      zzuf_socat_stop ();
      MHD_stop_daemon (d);
      return 1024;
    }
    start = time (NULL);
    while ((time (NULL) - start < 5) && (c != NULL))
    {
      max = 0;
      FD_ZERO (&rs);
      FD_ZERO (&ws);
      FD_ZERO (&es);
      curl_multi_perform (multi, &running);
      mret = curl_multi_fdset (multi, &rs, &ws, &es, &max);
      if (mret != CURLM_OK)
      {
        curl_multi_remove_handle (multi, c);
        curl_multi_cleanup (multi);
        curl_easy_cleanup (c);
        zzuf_socat_stop ();
        MHD_stop_daemon (d);
        curl_formfree (pd);
        return 2048;
      }
      if (MHD_YES != MHD_get_fdset (d, &rs, &ws, &es, &max))
      {
        curl_multi_remove_handle (multi, c);
        curl_multi_cleanup (multi);
        curl_easy_cleanup (c);
        curl_formfree (pd);
        zzuf_socat_stop ();
        MHD_stop_daemon (d);
        return 4096;
      }
      tv.tv_sec = 0;
      tv.tv_usec = 1000;
      select (max + 1, &rs, &ws, &es, &tv);
      curl_multi_perform (multi, &running);
      if (running == 0)
      {
        curl_multi_info_read (multi, &running);
        curl_multi_remove_handle (multi, c);
        curl_easy_cleanup (c);
        c = NULL;
      }
      MHD_run (d);
    }
    if (c != NULL)
    {
      curl_multi_remove_handle (multi, c);
      curl_easy_cleanup (c);
    }
    curl_formfree (pd);
  }
  fprintf (stderr, "\n");
  zzuf_socat_stop ();
  curl_multi_cleanup (multi);

  MHD_stop_daemon (d);
  return 0;
}


int
main (int argc, char *const *argv)
{
  unsigned int errorCount = 0;
  (void) argc;   /* Unused. Silent compiler warning. */

  oneone = (NULL != strrchr (argv[0], (int) '/')) ?
           (NULL != strstr (strrchr (argv[0], (int) '/'), "11")) : 0;
  if (0 != curl_global_init (CURL_GLOBAL_WIN32))
    return 2;
  if (MHD_YES == MHD_is_feature_supported (MHD_FEATURE_THREADS))
  {
    errorCount += testInternalPost ();
    errorCount += testMultithreadedPost ();
  }
  errorCount += testExternalPost ();
  if (errorCount != 0)
    fprintf (stderr, "Error (code: %u)\n", errorCount);
  curl_global_cleanup ();
  return (0 == errorCount) ? 0 : 1;       /* 0 == pass */
}
