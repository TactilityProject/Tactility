#pragma once

#include <Tactility/i18n/Localisation.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#ifndef L10N_APP
#error "Define L10N_APP as the name of the translations folder of the app (Translations/<name>) before including this header"
#endif

#ifdef CONFIG_TT_LANGUAGE_SELECTION

namespace {

/** The translations of the app (Translations/L10N_APP), opened on first use. Every source file of an app that translates includes this header. */
inline ::L10nDomain* getAppDomain() {
    static ::L10nDomain* domain = tt::i18n::openDomain(L10N_APP);
    return domain;
}

}

// Include this header before any <localisation/tr.h>
#define L10N_DOMAIN getAppDomain()
#include <localisation/tr.h>

#else

// English only: the texts are used as they are
#define tr(text) (text)
#define trc(context, text) (text)
#define tr_noop(text) text

#endif
