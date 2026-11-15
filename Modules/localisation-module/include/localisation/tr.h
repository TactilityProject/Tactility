// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <localisation/localisation.h>

/**
 * Translation shortcuts. Define L10N_DOMAIN to the app's domain, or leave it to only use the system domain:
 *
 *   static struct L10nDomain* app_domain;
 *   #define L10N_DOMAIN app_domain
 *   #include <localisation/tr.h>
 *   lv_label_set_text(label, tr("Delete"));
 */
#ifndef L10N_DOMAIN
#define L10N_DOMAIN ((struct L10nDomain*)0)
#endif

#define tr(text) localisation_get(L10N_DOMAIN, text)
#define trc(context, text) localisation_get_ctx(L10N_DOMAIN, context, text)
