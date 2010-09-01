/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* EffectSend implementation */

#include "sles_allinclusive.h"


/** \brief Maps AUX index to OutputMix interface index */

static const unsigned char AUX_to_MPH[AUX_MAX] = {
    MPH_ENVIRONMENTALREVERB,
    MPH_PRESETREVERB
};


/** \brief This is a private function that validates the effect interface specified by the
 *  application when it calls EnableEffectSend, IsEnabled, SetSendLevel, or GetSendLevel.
 *  For the interface to be valid, it has to satisfy these requirements:
 *   - object is an audio player (MIDI player is not supported yet)
 *   - audio sink is an output mix
 *   - interface was exposed at object creation time or by DynamicInterface::AddInterface
 *   - interface was "gotten" with Object::GetInterface
 */

static struct EnableLevel *getEnableLevel(IEffectSend *this, const void *pAuxEffect)
{
    // Make sure this effect send is on an audio player, not a MIDI player
    CAudioPlayer *audioPlayer = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
        (CAudioPlayer *) this->mThis : NULL;
    if (NULL == audioPlayer)
        return NULL;
    // Make sure the sink for this player is an output mix
    COutputMix *outputMix = audioPlayer->mOutputMix;
    if (NULL == outputMix)
        return NULL;
    unsigned aux;
    if (pAuxEffect == &outputMix->mEnvironmentalReverb.mItf)
        aux = AUX_ENVIRONMENTALREVERB;
    else if (pAuxEffect == &outputMix->mPresetReverb.mItf)
        aux = AUX_PRESETREVERB;
    else
        return NULL;
    assert(aux < AUX_MAX);
    // Validate that the application has a valid interface for the effect.  The interface must have
    // been exposed at object creation time or by DynamicInterface::AddInterface, and it also must
    // have been "gotten" with Object::GetInterface.
    int index = MPH_to_OutputMix[AUX_to_MPH[aux]];
    if (0 > index)
        return NULL;
    unsigned mask = 1 << index;
    object_lock_shared(&outputMix->mObject);
    SLuint32 state = outputMix->mObject.mInterfaceStates[index];
    mask &= outputMix->mObject.mGottenMask;
    object_unlock_shared(&outputMix->mObject);
    switch (state) {
    case INTERFACE_EXPOSED:
    case INTERFACE_ADDED:
    case INTERFACE_SUSPENDED:
    case INTERFACE_SUSPENDING:
    case INTERFACE_RESUMING_1:
    case INTERFACE_RESUMING_2:
        if (mask)
            return &this->mEnableLevels[aux];
        break;
    default:
        break;
    }
    return NULL;
}

#if defined(ANDROID) && !defined(USE_BACKPORT)
/** \brief This is a private function that translates an Android effect framework status code
 *  to the SL ES result code used in the EnableEffectSend() function of the SLEffectSendItf
 *  interface.
 */
static SLresult translateEnableFxSendError(android::status_t status) {
    switch (status) {
        case android::NO_ERROR:
            return SL_RESULT_SUCCESS;
            break;
        case android::INVALID_OPERATION:
        case android::BAD_VALUE:
        default:
            return SL_RESULT_RESOURCE_ERROR;
            break;
    }
}
#endif


static SLresult IEffectSend_EnableEffectSend(SLEffectSendItf self,
    const void *pAuxEffect, SLboolean enable, SLmillibel initialLevel)
{
    SL_ENTER_INTERFACE

    if (!((SL_MILLIBEL_MIN <= initialLevel) && (initialLevel <= 0))) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEffectSend *this = (IEffectSend *) self;
        struct EnableLevel *enableLevel = getEnableLevel(this, pAuxEffect);
        if (NULL == enableLevel) {
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            interface_lock_exclusive(this);
            enableLevel->mEnable = SL_BOOLEAN_FALSE != enable; // normalize
            enableLevel->mSendLevel = initialLevel;
#if !defined(ANDROID) || defined(USE_BACKPORT)
            result = SL_RESULT_SUCCESS;
#else
            // TODO do not repeat querying of CAudioPlayer, done inside getEnableLevel()
            CAudioPlayer *ap = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
                    (CAudioPlayer *) this->mThis : NULL;
            // check which effect the send is attached to, attach and set level
            if (NULL == ap) {
                result = SL_RESULT_RESOURCE_ERROR;
            } else {
                if (pAuxEffect == &ap->mOutputMix->mPresetReverb.mItf) {
                    result = translateEnableFxSendError(
                            android_fxSend_attach( ap, (bool) enable,
                                    ap->mOutputMix->mPresetReverb.mPresetReverbEffect,
                                    initialLevel) );
                } else if (pAuxEffect == &ap->mOutputMix->mEnvironmentalReverb.mItf) {
                    result = translateEnableFxSendError(
                            android_fxSend_attach( ap, (bool) enable,
                                    ap->mOutputMix->mEnvironmentalReverb.mEnvironmentalReverbEffect,
                                    initialLevel) );
                } else {
                    result = SL_RESULT_RESOURCE_ERROR;
                }
            }
#endif
            interface_unlock_exclusive(this);
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEffectSend_IsEnabled(SLEffectSendItf self,
    const void *pAuxEffect, SLboolean *pEnable)
{
    SL_ENTER_INTERFACE

    if (NULL == pEnable) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEffectSend *this = (IEffectSend *) self;
        struct EnableLevel *enableLevel = getEnableLevel(this, pAuxEffect);
        if (NULL == enableLevel) {
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            interface_lock_peek(this);
            SLboolean enable = enableLevel->mEnable;
            interface_unlock_peek(this);
            *pEnable = enable;
            result = SL_RESULT_SUCCESS;
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEffectSend_SetDirectLevel(SLEffectSendItf self, SLmillibel directLevel)
{
    SL_ENTER_INTERFACE

    if (!((SL_MILLIBEL_MIN <= directLevel) && (directLevel <= 0))) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEffectSend *this = (IEffectSend *) self;
        interface_lock_exclusive(this);
        SLmillibel oldDirectLevel = this->mDirectLevel;
        if (oldDirectLevel != directLevel) {
            this->mDirectLevel = directLevel;
#if defined(ANDROID) && !defined(USE_BACKPORT)
            CAudioPlayer *ap = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
                    (CAudioPlayer *) this->mThis : NULL;
            if (NULL != ap) {
                ap->mAmplFromDirectLevel = sles_to_android_amplification(directLevel);
            }
#endif
            interface_unlock_exclusive_attributes(this, ATTR_GAIN);
        } else {
            interface_unlock_exclusive(this);
        }
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEffectSend_GetDirectLevel(SLEffectSendItf self, SLmillibel *pDirectLevel)
{
    SL_ENTER_INTERFACE

    if (NULL == pDirectLevel) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEffectSend *this = (IEffectSend *) self;
        interface_lock_peek(this);
        SLmillibel directLevel = this->mDirectLevel;
        interface_unlock_peek(this);
        *pDirectLevel = directLevel;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEffectSend_SetSendLevel(SLEffectSendItf self, const void *pAuxEffect,
    SLmillibel sendLevel)
{
    SL_ENTER_INTERFACE

    if (!((SL_MILLIBEL_MIN <= sendLevel) && (sendLevel <= 0))) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEffectSend *this = (IEffectSend *) self;
        struct EnableLevel *enableLevel = getEnableLevel(this, pAuxEffect);
        if (NULL == enableLevel) {
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            // EnableEffectSend is exclusive, so this has to be also
            interface_lock_exclusive(this);
            enableLevel->mSendLevel = sendLevel;
#if defined(ANDROID) && !defined(USE_BACKPORT)
            CAudioPlayer *ap = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
                    (CAudioPlayer *) this->mThis : NULL;
            if (NULL != ap) {
                android_fxSend_setSendLevel(ap, sendLevel);
            }
#endif
            interface_unlock_exclusive(this);
            result = SL_RESULT_SUCCESS;
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEffectSend_GetSendLevel(SLEffectSendItf self, const void *pAuxEffect,
    SLmillibel *pSendLevel)
{
    SL_ENTER_INTERFACE

    if (NULL == pSendLevel) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEffectSend *this = (IEffectSend *) self;
        struct EnableLevel *enableLevel = getEnableLevel(this, pAuxEffect);
        if (NULL == enableLevel) {
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            interface_lock_peek(this);
            SLmillibel sendLevel = enableLevel->mSendLevel;
            interface_unlock_peek(this);
            *pSendLevel = sendLevel;
            result = SL_RESULT_SUCCESS;
        }
    }

    SL_LEAVE_INTERFACE
}


static const struct SLEffectSendItf_ IEffectSend_Itf = {
    IEffectSend_EnableEffectSend,
    IEffectSend_IsEnabled,
    IEffectSend_SetDirectLevel,
    IEffectSend_GetDirectLevel,
    IEffectSend_SetSendLevel,
    IEffectSend_GetSendLevel
};

void IEffectSend_init(void *self)
{
    IEffectSend *this = (IEffectSend *) self;
    this->mItf = &IEffectSend_Itf;
    this->mDirectLevel = 0;
    struct EnableLevel *enableLevel = this->mEnableLevels;
    unsigned aux;
    for (aux = 0; aux < AUX_MAX; ++aux, ++enableLevel) {
        enableLevel->mEnable = SL_BOOLEAN_FALSE;
        enableLevel->mSendLevel = SL_MILLIBEL_MIN;
    }
}
