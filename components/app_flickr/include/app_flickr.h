/**
 * @file app_flickr.h
 * @brief Mirrors the images of Flickr photo feeds onto the SD card so the
 *        photo frame shows them.
 *
 * For every feed URL saved with settings_set_flickr_feeds() (RSS 2.0 or Atom -
 * e.g. https://www.flickr.com/services/feeds/photos_public.gne?id=<user-id>&format=rss2),
 * app_flickr_sync() downloads the feed, then downloads each item's image into
 * its own folder, CONFIG_APP_PHOTO_DIR/flickr/<feed-hash>/, where the photo
 * frame's recursive scan picks it up like any other photo. The folder is a
 * mirror of the feed: images that drop out of the feed are deleted, as is a
 * whole folder once its feed is removed - so nothing else should be stored
 * under CONFIG_APP_PHOTO_DIR/flickr (the web photo manager hides that folder).
 *
 * There is no task of its own: net_worker calls app_flickr_sync() between the
 * weather/stock requests, so only one TLS session is open at a time (each one
 * costs tens of KB of internal RAM). Request an immediate sync by posting
 * APP_EVT_REQ_FLICKR_SYNC (after the feed list changes).
 *
 * Images are fetched at Flickr's 800px ("_c") size where the URL allows it,
 * rather than the feed's default 1024px, to match the 800x480 panel and keep
 * downloads small and decodes cheap.
 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FLICKR_DIR          CONFIG_APP_PHOTO_DIR "/flickr"
#define APP_FLICKR_ERROR_MAX    64

typedef struct {
    bool   syncing;         /* a sync is running right now */
    bool   have_error;      /* the most recent sync had at least one failure */
    char   error[APP_FLICKR_ERROR_MAX]; /* short reason, valid when have_error */
    time_t last_sync;       /* when the most recent sync finished (0 = never) */
    int    image_count;     /* Flickr images on the SD card after that sync */
    int    unsupported;     /* of those, progressive JPEGs the photo frame can't decode */
} app_flickr_status_t;

/** Allocate buffers. Call once before net_worker_start(). */
esp_err_t app_flickr_init(void);

/** True if at least one feed is configured. */
bool app_flickr_has_feeds(void);

/** Bring the SD card in line with the configured feeds. Blocking (up to a
 *  minute or two for a new feed); only call from net_worker.
 *  @param online false: only delete folders of removed feeds (no network).
 *  @return ESP_OK if every feed and image could be fetched. */
esp_err_t app_flickr_sync(bool online);

/** Copy out the current sync status (any task). */
void app_flickr_get_status(app_flickr_status_t *out);

#ifdef __cplusplus
}
#endif
