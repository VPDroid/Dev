/*
 * Copyright (C) 2014 The Android Open Source Project
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

package android.media.audiopolicy;

import android.annotation.IntDef;
import android.annotation.SystemApi;
import android.media.AudioFormat;
import android.media.AudioSystem;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Objects;

/**
 * @hide
 */
@SystemApi
public class AudioMix {

    private AudioMixingRule mRule;
    private AudioFormat mFormat;
    private int mRouteFlags;
    private String mRegistrationId;
    private int mMixType = MIX_TYPE_INVALID;
    int mMixState = MIX_STATE_DISABLED;
    int mCallbackFlags;

    /**
     * All parameters are guaranteed valid through the Builder.
     */
    private AudioMix(AudioMixingRule rule, AudioFormat format, int routeFlags, int callbackFlags) {
        mRule = rule;
        mFormat = format;
        mRouteFlags = routeFlags;
        mRegistrationId = null;
        mMixType = rule.getTargetMixType();
        mCallbackFlags = callbackFlags;
    }

    // CALLBACK_FLAG_* values: keep in sync with AudioMix::kCbFlag* values defined
    // in frameworks/av/include/media/AudioPolicy.h
    /** @hide */
    public final static int CALLBACK_FLAG_NOTIFY_ACTIVITY = 0x1;
    // when adding new MIX_FLAG_* flags, add them to this mask of authorized masks:
    private final static int CALLBACK_FLAGS_ALL = CALLBACK_FLAG_NOTIFY_ACTIVITY;

    // ROUTE_FLAG_* values: keep in sync with MIX_ROUTE_FLAG_* values defined
    // in frameworks/av/include/media/AudioPolicy.h
    /**
     * An audio mix behavior where the output of the mix is sent to the original destination of
     * the audio signal, i.e. an output device for an output mix, or a recording for an input mix.
     */
    @SystemApi
    public static final int ROUTE_FLAG_RENDER    = 0x1;
    /**
     * An audio mix behavior where the output of the mix is rerouted back to the framework and
     * is accessible for injection or capture through the {@link AudioTrack} and {@link AudioRecord}
     * APIs.
     */
    @SystemApi
    public static final int ROUTE_FLAG_LOOP_BACK = 0x1 << 1;

    // MIX_TYPE_* values to keep in sync with frameworks/av/include/media/AudioPolicy.h
    /**
     * @hide
     * Invalid mix type, default value.
     */
    public static final int MIX_TYPE_INVALID = -1;
    /**
     * @hide
     * Mix type indicating playback streams are mixed.
     */
    public static final int MIX_TYPE_PLAYERS = 0;
    /**
     * @hide
     * Mix type indicating recording streams are mixed.
     */
    public static final int MIX_TYPE_RECORDERS = 1;


    // MIX_STATE_* values to keep in sync with frameworks/av/include/media/AudioPolicy.h
    /**
     * @hide
     * State of a mix before its policy is enabled.
     */
    @SystemApi
    public static final int MIX_STATE_DISABLED = -1;
    /**
     * @hide
     * State of a mix when there is no audio to mix.
     */
    @SystemApi
    public static final int MIX_STATE_IDLE = 0;
    /**
     * @hide
     * State of a mix that is actively mixing audio.
     */
    @SystemApi
    public static final int MIX_STATE_MIXING = 1;

    /**
     * @hide
     * The current mixing state.
     * @return one of {@link #MIX_STATE_DISABLED}, {@link #MIX_STATE_IDLE},
     *          {@link #MIX_STATE_MIXING}.
     */
    @SystemApi
    public int getMixState() {
        return mMixState;
    }


    int getRouteFlags() {
        return mRouteFlags;
    }

    AudioFormat getFormat() {
        return mFormat;
    }

    AudioMixingRule getRule() {
        return mRule;
    }

    /** @hide */
    public int getMixType() {
        return mMixType;
    }

    void setRegistration(String regId) {
        mRegistrationId = regId;
    }

    /** @hide */
    public String getRegistration() {
        return mRegistrationId;
    }

    /** @hide */
    @Override
    public int hashCode() {
        return Objects.hash(mRouteFlags, mRule, mMixType, mFormat);
    }

    /** @hide */
    @IntDef(flag = true,
            value = { ROUTE_FLAG_RENDER, ROUTE_FLAG_LOOP_BACK } )
    @Retention(RetentionPolicy.SOURCE)
    public @interface RouteFlags {}

    /**
     * Builder class for {@link AudioMix} objects
     *
     */
    @SystemApi
    public static class Builder {
        private AudioMixingRule mRule = null;
        private AudioFormat mFormat = null;
        private int mRouteFlags = 0;
        private int mCallbackFlags = 0;

        /**
         * @hide
         * Only used by AudioPolicyConfig, not a public API.
         */
        Builder() { }

        /**
         * Construct an instance for the given {@link AudioMixingRule}.
         * @param rule a non-null {@link AudioMixingRule} instance.
         * @throws IllegalArgumentException
         */
        @SystemApi
        public Builder(AudioMixingRule rule)
                throws IllegalArgumentException {
            if (rule == null) {
                throw new IllegalArgumentException("Illegal null AudioMixingRule argument");
            }
            mRule = rule;
        }

        /**
         * @hide
         * Only used by AudioPolicyConfig, not a public API.
         * @param rule
         * @return the same Builder instance.
         * @throws IllegalArgumentException
         */
        public Builder setMixingRule(AudioMixingRule rule)
                throws IllegalArgumentException {
            if (rule == null) {
                throw new IllegalArgumentException("Illegal null AudioMixingRule argument");
            }
            mRule = rule;
            return this;
        }

        /**
         * @hide
         * Only used by AudioPolicyConfig, not a public API.
         * @param callbackFlags which callbacks are called from native
         * @return the same Builder instance.
         * @throws IllegalArgumentException
         */
        public Builder setCallbackFlags(int flags) throws IllegalArgumentException {
            if ((flags != 0) && ((flags & CALLBACK_FLAGS_ALL) == 0)) {
                throw new IllegalArgumentException("Illegal callback flags 0x"
                        + Integer.toHexString(flags).toUpperCase());
            }
            mCallbackFlags = flags;
            return this;
        }

        /**
         * Sets the {@link AudioFormat} for the mix.
         * @param format a non-null {@link AudioFormat} instance.
         * @return the same Builder instance.
         * @throws IllegalArgumentException
         */
        @SystemApi
        public Builder setFormat(AudioFormat format)
                throws IllegalArgumentException {
            if (format == null) {
                throw new IllegalArgumentException("Illegal null AudioFormat argument");
            }
            mFormat = format;
            return this;
        }

        /**
         * Sets the routing behavior for the mix.
         * @param routeFlags one of {@link AudioMix#ROUTE_FLAG_LOOP_BACK},
         *     {@link AudioMix#ROUTE_FLAG_RENDER}
         * @return the same Builder instance.
         * @throws IllegalArgumentException
         */
        @SystemApi
        public Builder setRouteFlags(@RouteFlags int routeFlags)
                throws IllegalArgumentException {
            if (routeFlags == 0) {
                throw new IllegalArgumentException("Illegal empty route flags");
            }
            if ((routeFlags & (ROUTE_FLAG_LOOP_BACK | ROUTE_FLAG_RENDER)) == 0) {
                throw new IllegalArgumentException("Invalid route flags 0x"
                        + Integer.toHexString(routeFlags) + "when creating an AudioMix");
            }
            mRouteFlags = routeFlags;
            return this;
        }

        /**
         * Combines all of the settings and return a new {@link AudioMix} object.
         * @return a new {@link AudioMix} object
         * @throws IllegalArgumentException if no {@link AudioMixingRule} has been set.
         */
        @SystemApi
        public AudioMix build() throws IllegalArgumentException {
            if (mRule == null) {
                throw new IllegalArgumentException("Illegal null AudioMixingRule");
            }
            if (mRouteFlags == 0) {
                // no route flags set, use default
                mRouteFlags = ROUTE_FLAG_RENDER;
            }
            if (mFormat == null) {
                int rate = AudioSystem.getPrimaryOutputSamplingRate();
                if (rate <= 0) {
                    rate = 44100;
                }
                mFormat = new AudioFormat.Builder().setSampleRate(rate).build();
            }
            return new AudioMix(mRule, mFormat, mRouteFlags, mCallbackFlags);
        }
    }
}
