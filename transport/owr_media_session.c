/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

/*/
\*\ OwrMediaSession
/*/


/**
 * SECTION:owr_media_session
 * @short_description: OwrMediaSession
 * @title: OwrMediaSession
 *
 * OwrMediaSession - Represents one incoming and one outgoing media stream.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_media_session.h"

#include "owr_media_session_private.h"
#include "owr_media_source.h"
#include "owr_private.h"
#include "owr_remote_media_source.h"
#include "owr_session_private.h"

#include <string.h>


#define OWR_MEDIA_SESSION_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE((obj), OWR_TYPE_MEDIA_SESSION, OwrMediaSessionPrivate))

G_DEFINE_TYPE(OwrMediaSession, owr_media_session, OWR_TYPE_SESSION)

struct _OwrMediaSessionPrivate {
    gboolean rtcp_mux;
    gchar *incoming_srtp_key;
    gchar *outgoing_srtp_key;
    guint send_ssrc;
    gchar *cname;
    GRWLock rw_lock;
    OwrPayload *send_payload;
    OwrMediaSource *send_source;
    GPtrArray *receive_payloads;
    GClosure *on_send_payload;
    GClosure *on_send_source;
};

enum {
    SIGNAL_ON_NEW_STATS,
    SIGNAL_ON_INCOMING_SOURCE,

    LAST_SIGNAL
};

#define DEFAULT_RTCP_MUX FALSE

enum {
    PROP_0,

    PROP_RTCP_MUX,
    PROP_INCOMING_SRTP_KEY,
    PROP_OUTGOING_SRTP_KEY,
    PROP_SEND_SSRC,
    PROP_CNAME,

    N_PROPERTIES
};

static guint media_session_signals[LAST_SIGNAL] = { 0 };
static GParamSpec *obj_properties[N_PROPERTIES] = {NULL, };


static gboolean add_receive_payload(GHashTable *args);
static gboolean set_send_payload(GHashTable *args);
static gboolean set_send_source(GHashTable *args);


static void owr_media_session_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    OwrMediaSessionPrivate *priv = OWR_MEDIA_SESSION(object)->priv;
    GObjectClass *parent_class;

    switch (property_id) {
    case PROP_RTCP_MUX:
        priv->rtcp_mux = g_value_get_boolean(value);
        break;

    case PROP_INCOMING_SRTP_KEY:
        if (priv->incoming_srtp_key)
            g_free(priv->incoming_srtp_key);
        priv->incoming_srtp_key = g_value_dup_string(value);
        g_warn_if_fail(strlen(priv->incoming_srtp_key) == 40);
        break;

    case PROP_OUTGOING_SRTP_KEY:
        if (priv->outgoing_srtp_key)
            g_free(priv->outgoing_srtp_key);
        priv->outgoing_srtp_key = g_value_dup_string(value);
        break;

    default:
        parent_class = g_type_class_peek_parent(OWR_MEDIA_SESSION_GET_CLASS(object));
        parent_class->set_property(object, property_id, value, pspec);
        break;
    }
}

static void owr_media_session_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    OwrMediaSessionPrivate *priv = OWR_MEDIA_SESSION(object)->priv;
    GObjectClass *parent_class;

    switch (property_id) {
    case PROP_RTCP_MUX:
        g_value_set_boolean(value, priv->rtcp_mux);
        break;

    case PROP_INCOMING_SRTP_KEY:
        g_value_set_string(value, priv->incoming_srtp_key);
        break;

    case PROP_OUTGOING_SRTP_KEY:
        g_value_set_string(value, priv->outgoing_srtp_key);
        break;

    case PROP_SEND_SSRC:
        g_value_set_uint(value, priv->send_ssrc);
        break;

    case PROP_CNAME:
        g_value_set_string(value, priv->cname);
        break;

    default:
        parent_class = g_type_class_peek_parent(OWR_MEDIA_SESSION_GET_CLASS(object));
        parent_class->get_property(object, property_id, value, pspec);
        break;
    }
}


static void owr_media_session_on_incoming_source(OwrMediaSession *media_session, OwrRemoteMediaSource *source)
{
    g_warn_if_fail(media_session);
    g_warn_if_fail(source);

    g_object_unref(source);
}

static void owr_media_session_on_new_stats(OwrMediaSession *media_session, GHashTable *stats)
{
    g_warn_if_fail(media_session);
    g_warn_if_fail(stats);

    g_hash_table_unref(stats);
}

static void owr_media_session_finalize(GObject *object)
{
    OwrMediaSession *media_session = OWR_MEDIA_SESSION(object);
    OwrMediaSessionPrivate *priv = media_session->priv;

    if (priv->incoming_srtp_key)
        g_free(priv->incoming_srtp_key);
    if (priv->outgoing_srtp_key)
        g_free(priv->outgoing_srtp_key);

    if (priv->cname)
        g_free(priv->cname);

    if (priv->send_source)
        g_object_unref(priv->send_source);

    if (priv->send_payload)
        g_object_unref(priv->send_payload);
    g_ptr_array_unref(priv->receive_payloads);
    g_rw_lock_clear(&priv->rw_lock);

    G_OBJECT_CLASS(owr_media_session_parent_class)->finalize(object);
}

static void owr_media_session_class_init(OwrMediaSessionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(OwrMediaSessionPrivate));

    klass->on_new_stats = owr_media_session_on_new_stats;
    klass->on_incoming_source = owr_media_session_on_incoming_source;

    /**
     * OwrMediaSession::on-new-stats:
     * @media_session: the #OwrMediaSession object which received the signal
     * @stats: (element-type utf8 GValue) (transfer none): the stats #GHashTable
     *
     * Notify of new stats for a #OwrMediaSession.
     */
    media_session_signals[SIGNAL_ON_NEW_STATS] = g_signal_new("on-new-stats",
        G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_CLEANUP,
        G_STRUCT_OFFSET(OwrMediaSessionClass, on_new_stats), NULL, NULL,
        g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, G_TYPE_HASH_TABLE);

    /**
    * OwrMediaSession::on-incoming-source:
    * @media_session: the object which received the signal
    * @source: (transfer none): the new incoming source
    *
    * Notify of a new incoming source for a #OwrMediaSession.
    */
    media_session_signals[SIGNAL_ON_INCOMING_SOURCE] = g_signal_new("on-incoming-source",
        G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_FIRST,
        G_STRUCT_OFFSET(OwrMediaSessionClass, on_incoming_source), NULL, NULL,
        g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, OWR_TYPE_REMOTE_MEDIA_SOURCE);

    gobject_class->set_property = owr_media_session_set_property;
    gobject_class->get_property = owr_media_session_get_property;
    gobject_class->finalize = owr_media_session_finalize;

    obj_properties[PROP_RTCP_MUX] = g_param_spec_boolean("rtcp-mux", "RTP/RTCP multiplexing",
        "Whether to use RTP/RTCP multiplexing or not",
        DEFAULT_RTCP_MUX,
        G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_INCOMING_SRTP_KEY] = g_param_spec_string("incoming-srtp-key",
        "Incoming SRTP key", "Key used to decrypt incoming SRTP packets (base64 encoded)",
        NULL, G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_OUTGOING_SRTP_KEY] = g_param_spec_string("outgoing-srtp-key",
        "Outgoing SRTP key", "Key used to encrypt outgoing SRTP packets (base64 encoded)",
        NULL, G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_SEND_SSRC] = g_param_spec_uint("send-ssrc", "Send ssrc",
        "The ssrc used for the outgoing RTP media stream",
        0, G_MAXUINT, 0, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    obj_properties[PROP_CNAME] = g_param_spec_string("cname", "CNAME",
        "The canonical name identifying this endpoint",
        NULL, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);

}

static void owr_media_session_init(OwrMediaSession *media_session)
{
    OwrMediaSessionPrivate *priv;

    media_session->priv = priv = OWR_MEDIA_SESSION_GET_PRIVATE(media_session);
    priv->rtcp_mux = DEFAULT_RTCP_MUX;
    priv->incoming_srtp_key = NULL;
    priv->outgoing_srtp_key = NULL;
    priv->send_ssrc = 0;
    priv->cname = NULL;
    priv->send_payload = NULL;
    priv->send_source = NULL;
    priv->receive_payloads = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
    priv->on_send_payload = NULL;
    priv->on_send_source = NULL;
    g_rw_lock_init(&priv->rw_lock);
}

/**
 * owr_media_session_new:
 * @dtls_client_mode: TRUE if the connection for the session should be setup using DTLS client role
 *
 * Constructs the OwrMediaSession object.
 *
 * Returns: the created media session
 */
OwrMediaSession * owr_media_session_new(gboolean dtls_client_mode)
{
    return g_object_new(OWR_TYPE_MEDIA_SESSION, "dtls-client-mode", dtls_client_mode, NULL);
}


/**
 * owr_media_session_add_receive_payload:
 * @media_session: the media session on which to add the receive payload.
 * @payload: the receive payload to add
 *
 * The function adds support for receiving the given payload type.
 */
void owr_media_session_add_receive_payload(OwrMediaSession *media_session, OwrPayload *payload)
{
    GHashTable *args;

    g_return_if_fail(media_session);
    g_return_if_fail(payload);

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "media_session", media_session);
    g_hash_table_insert(args, "payload", payload);

    g_object_ref(media_session);
    g_object_ref(payload);
    _owr_schedule_with_hash_table((GSourceFunc)add_receive_payload, args);
}


/**
 * owr_media_session_set_send_payload:
 * @media_session: The media session on which set the send payload.
 * @payload: the send payload to set
 *
 * Sets what payload that will be sent.
 */
void owr_media_session_set_send_payload(OwrMediaSession *media_session, OwrPayload *payload)
{
    GHashTable *args;

    g_return_if_fail(media_session);
    g_return_if_fail(payload);

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "media_session", media_session);
    g_hash_table_insert(args, "payload", payload);
    g_object_ref(media_session);
    g_object_ref(payload);

    _owr_schedule_with_hash_table((GSourceFunc)set_send_payload, args);
}


/**
 * owr_media_session_set_send_source:
 * @media_session: The media session on which to set the send source.
 * @source: the send source to set
 *
 * Sets the source from which data will be sent.
 */
void owr_media_session_set_send_source(OwrMediaSession *media_session, OwrMediaSource *source)
{
    GHashTable *args;

    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));
    g_return_if_fail(OWR_IS_MEDIA_SOURCE(source));

    args = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(args, "media_session", media_session);
    g_hash_table_insert(args, "source", source);
    g_object_ref(media_session);
    g_object_ref(source);

    _owr_schedule_with_hash_table((GSourceFunc)set_send_source, args);
}



/* Internal functions */

static gboolean add_receive_payload(GHashTable *args)
{
    guint i = 0, payload_type = 0, plt = 0;
    gboolean payload_found = FALSE;
    GPtrArray *payloads = NULL;
    OwrMediaSession *media_session = NULL;
    OwrPayload *payload = NULL;

    g_return_val_if_fail(args, FALSE);

    media_session = g_hash_table_lookup(args, "media_session");
    payload = g_hash_table_lookup(args, "payload");

    g_return_val_if_fail(media_session, FALSE);
    g_return_val_if_fail(payload, FALSE);

    g_object_get(payload, "payload-type", &payload_type, NULL);

    g_rw_lock_writer_lock(&media_session->priv->rw_lock);
    payloads = media_session->priv->receive_payloads;
    g_return_val_if_fail(payloads, FALSE);

    for (i = 0; i < payloads->len; i++) {
        OwrPayload *p = g_ptr_array_index(payloads, i);
        g_object_get(p, "payload-type", &plt, NULL);
        if (p == payload || plt == payload_type) {
            payload_found = TRUE;
            break;
        }
    }
    g_rw_lock_writer_unlock(&media_session->priv->rw_lock);

    if (!payload_found) {
        g_ptr_array_add(payloads, payload);
        g_object_ref(payload);
    } else
        g_warning("An already existing payload was added to the media session. Action aborted.\n");

    g_object_unref(payload);
    g_object_unref(media_session);
    g_hash_table_unref(args);
    return FALSE;
}

static gboolean set_send_payload(GHashTable *args)
{
    OwrMediaSession *media_session;
    OwrMediaSessionPrivate *priv;
    OwrPayload *payload;
    GValue params[1] = { G_VALUE_INIT };

    g_return_val_if_fail(args, FALSE);

    media_session = g_hash_table_lookup(args, "media_session");
    payload = g_hash_table_lookup(args, "payload");

    g_return_val_if_fail(media_session, FALSE);
    g_return_val_if_fail(payload, FALSE);

    priv = media_session->priv;

    if (priv->send_payload)
        g_object_unref(priv->send_payload);

    priv->send_payload = payload;

    if (priv->on_send_payload) {
        g_value_init(&params[0], OWR_TYPE_MEDIA_SESSION);
        g_value_set_instance(&params[0], media_session);
        g_closure_invoke(priv->on_send_payload, NULL, 1, (const GValue *)&params, NULL);
    }

    g_object_unref(media_session);
    g_hash_table_unref(args);
    return FALSE;
}

static gboolean set_send_source(GHashTable *args)
{
    OwrMediaSession *media_session;
    OwrMediaSessionPrivate *priv;
    OwrMediaSource *source;
    GValue params[1] = { G_VALUE_INIT };

    g_return_val_if_fail(args, FALSE);

    media_session = g_hash_table_lookup(args, "media_session");
    source = g_hash_table_lookup(args, "source");

    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), FALSE);
    g_return_val_if_fail(OWR_IS_MEDIA_SOURCE(source), FALSE);

    priv = media_session->priv;

    if (priv->send_source)
        g_object_unref(priv->send_source);

    priv->send_source = source;

    if (priv->on_send_source) {
        g_value_init(&params[0], OWR_TYPE_MEDIA_SESSION);
        g_value_set_instance(&params[0], media_session);
        g_closure_invoke(priv->on_send_source, NULL, 1, (const GValue *)&params, NULL);
    }

    g_object_unref(source);
    g_object_unref(media_session);
    g_hash_table_unref(args);

    return FALSE;
}


/* Private methods */

OwrPayload * _owr_media_session_get_receive_payload(OwrMediaSession *media_session, guint32 payload_type)
{
    GPtrArray *receive_payloads = media_session->priv->receive_payloads;
    OwrPayload *payload = NULL;
    guint32 i = 0, pt = 0;

    g_return_val_if_fail(media_session, NULL);
    g_return_val_if_fail(receive_payloads, NULL);

    g_rw_lock_reader_lock(&media_session->priv->rw_lock);
    for (i = 0; i < receive_payloads->len; i++) {
        payload = g_ptr_array_index(receive_payloads, i);
        g_object_get(payload, "payload-type", &pt, NULL);
        if (pt == payload_type)
            break;
    }
    g_object_ref(payload);
    g_rw_lock_reader_unlock(&media_session->priv->rw_lock);

    if (pt == payload_type)
        return payload;
    return NULL;
}

OwrPayload * _owr_media_session_get_send_payload(OwrMediaSession *media_session)
{
    g_return_val_if_fail(media_session, NULL);

    return media_session->priv->send_payload;
}

OwrMediaSource * _owr_media_session_get_send_source(OwrMediaSession *media_session)
{
    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), NULL);

    return media_session->priv->send_source;
}

void _owr_media_session_set_on_send_payload(OwrMediaSession *media_session, GClosure *on_send_payload)
{
    g_return_if_fail(media_session);
    g_return_if_fail(on_send_payload);

    media_session->priv->on_send_payload = on_send_payload;
    g_closure_set_marshal(media_session->priv->on_send_payload, g_cclosure_marshal_VOID__VOID);
}

void _owr_media_session_set_on_send_source(OwrMediaSession *media_session, GClosure *on_send_source)
{
    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));
    g_return_if_fail(on_send_source);

    media_session->priv->on_send_source = on_send_source;
    g_closure_set_marshal(media_session->priv->on_send_source, g_cclosure_marshal_VOID__VOID);
}

void _owr_media_session_clear_closures(OwrMediaSession *media_session)
{
    if (media_session->priv->on_send_payload) {
        g_closure_invalidate(media_session->priv->on_send_payload);
        g_closure_unref(media_session->priv->on_send_payload);
        media_session->priv->on_send_payload = NULL;
    }

    if (media_session->priv->on_send_source) {
        g_closure_invalidate(media_session->priv->on_send_source);
        g_closure_unref(media_session->priv->on_send_source);
        media_session->priv->on_send_source = NULL;
    }

    _owr_session_clear_closures(OWR_SESSION(media_session));
}

GstBuffer * _owr_media_session_get_srtp_key_buffer(OwrMediaSession *media_session, const gchar *keyname)
{
    gchar *base64_key;
    guchar *key;
    gsize key_len;

    g_return_val_if_fail(OWR_IS_MEDIA_SESSION(media_session), NULL);
    g_return_val_if_fail(!g_strcmp0(keyname, "incoming-srtp-key")
        || !g_strcmp0(keyname, "outgoing-srtp-key"), NULL);

    g_object_get(media_session, keyname, &base64_key, NULL);
    if (!base64_key || !base64_key[0])
        return gst_buffer_new_wrapped(g_new0(gchar, 1), 1);

    key = g_base64_decode(base64_key, &key_len);
    g_warn_if_fail(key_len == 30);
    return gst_buffer_new_wrapped(key, key_len);
}

void _owr_media_session_set_send_ssrc(OwrMediaSession *media_session, guint send_ssrc)
{
    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));
    media_session->priv->send_ssrc = send_ssrc;
}

void _owr_media_session_set_cname(OwrMediaSession *media_session, const gchar *cname)
{
    g_return_if_fail(OWR_IS_MEDIA_SESSION(media_session));
    media_session->priv->cname = g_strdup(cname);
}
