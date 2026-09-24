/**
 * @file photo_feed.h
 * @brief Mirrors the images of RSS 2.0 / Atom photo feeds onto the SD card so
 *        the photo frame shows them.
 *
 * For every feed URL saved with settings_set_photo_feeds() (e.g. a Flickr feed
 * https://www.flickr.com/services/feeds/photos_public.gne?id=<user-id>&format=rss2,
 * or any feed that carries images), photo_feed_sync() downloads the feed, takes
 * one image per item and downloads it into the feed's own folder,
 * CONFIG_APP_PHOTO_DIR/feeds/<feed-hash>/, where the photo frame's recursive
 * scan picks it up like any other photo. The folder is a mirror of the feed:
 * images that drop out of the feed are deleted, as is a whole folder once its
 * feed is removed - so nothing else should be stored under PHOTO_FEED_DIR
 * (the web photo manager shows it read-only).
 *
 * Image per item, first match wins: <media:content url>, <enclosure url>,
 * Atom <link rel="enclosure" href> (skipped when their type/medium says it is
 * not an image), <media:thumbnail url>, then the first <img src> in the item's
 * HTML (<description>, <content:encoded>, escaped or CDATA). Only JPEG and PNG
 * are kept (checked from the file's first bytes); anything else is remembered
 * as "<name>.skip" so it is not downloaded again.
 *
 * There is no task of its own: net_worker calls photo_feed_sync() between the
 * weather/stock requests, so only one TLS session is open at a time (each one
 * costs tens of KB of internal RAM). Request an immediate sync by posting
 * APP_EVT_REQ_FEED_SYNC (after the feed list changes).
 *
 * Flickr image URLs are rewritten from the feed's 1024px ("_b") size to 800px
 * ("_c") to match the 800x480 panel and keep downloads small.
 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTO_FEED_DIR_NAME     "feeds"
#define PHOTO_FEED_DIR          CONFIG_APP_PHOTO_DIR "/" PHOTO_FEED_DIR_NAME
#define PHOTO_FEED_OLD_DIR      CONFIG_APP_PHOTO_DIR "/flickr"   /* up to v0.2.2, renamed on first sync */
#define PHOTO_FEED_ERROR_MAX    64

typedef struct {
    bool   syncing;         /* a sync is running right now */
    bool   have_error;      /* the most recent sync had at least one failure */
    char   error[PHOTO_FEED_ERROR_MAX]; /* short reason, valid when have_error */
    time_t last_sync;       /* when the most recent sync finished (0 = never) */
    int    image_count;     /* feed images on the SD card after that sync */
    int    unsupported;     /* of those, progressive JPEGs the photo frame can't decode */
    int    skipped;         /* items whose image isn't JPEG/PNG (e.g. WebP, GIF) */
} photo_feed_status_t;

/** Allocate buffers. Call once before net_worker_start(). */
esp_err_t photo_feed_init(void);

/** True if at least one feed is configured. */
bool photo_feed_has_feeds(void);

/** Bring the SD card in line with the configured feeds. Blocking (up to a
 *  minute or two for a new feed); only call from net_worker.
 *  @param online false: only delete folders of removed feeds (no network).
 *  @return ESP_OK if every feed and image could be fetched. */
esp_err_t photo_feed_sync(bool online);

/** Copy out the current sync status (any task). */
void photo_feed_get_status(photo_feed_status_t *out);

#ifdef __cplusplus
}
#endif
