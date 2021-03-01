/*
 * Copyright (C) 2008 The Android Open Source Project
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

package com.android.internal.telephony;

import com.android.internal.telephony.GsmAlphabet.TextEncodingDetails;
import com.android.internal.telephony.SmsConstants;
import com.android.internal.telephony.SmsHeader;
import java.text.BreakIterator;
import java.util.Arrays;

import android.provider.Telephony;
import android.telephony.SmsMessage;

/**
 * Base class declaring the specific methods and members for SmsMessage.
 * {@hide}
 */
public abstract class SmsMessageBase {
    /** {@hide} The address of the SMSC. May be null */
    protected String mScAddress;

    /** {@hide} The address of the sender */
    protected SmsAddress mOriginatingAddress;

    /** {@hide} The message body as a string. May be null if the message isn't text */
    protected String mMessageBody;

    /** {@hide} */
    protected String mPseudoSubject;

    /** {@hide} Non-null if this is an email gateway message */
    protected String mEmailFrom;

    /** {@hide} Non-null if this is an email gateway message */
    protected String mEmailBody;

    /** {@hide} */
    protected boolean mIsEmail;

    /** {@hide} */
    protected long mScTimeMillis;

    /** {@hide} The raw PDU of the message */
    protected byte[] mPdu;

    /** {@hide} The raw bytes for the user data section of the message */
    protected byte[] mUserData;

    /** {@hide} */
    protected SmsHeader mUserDataHeader;

    // "Message Waiting Indication Group"
    // 23.038 Section 4
    /** {@hide} */
    protected boolean mIsMwi;

    /** {@hide} */
    protected boolean mMwiSense;

    /** {@hide} */
    protected boolean mMwiDontStore;

    /**
     * Indicates status for messages stored on the ICC.
     */
    protected int mStatusOnIcc = -1;

    /**
     * Record index of message in the EF.
     */
    protected int mIndexOnIcc = -1;

    /** TP-Message-Reference - Message Reference of sent message. @hide */
    public int mMessageRef;

    // TODO(): This class is duplicated in SmsMessage.java. Refactor accordingly.
    public static abstract class SubmitPduBase  {
        public byte[] encodedScAddress; // Null if not applicable.
        public byte[] encodedMessage;

        @Override
        public String toString() {
            return "SubmitPdu: encodedScAddress = "
                    + Arrays.toString(encodedScAddress)
                    + ", encodedMessage = "
                    + Arrays.toString(encodedMessage);
        }
    }

    /**
     * Returns the address of the SMS service center that relayed this message
     * or null if there is none.
     */
    public String getServiceCenterAddress() {
        return mScAddress;
    }

    /**
     * Returns the originating address (sender) of this SMS message in String
     * form or null if unavailable
     */
    public String getOriginatingAddress() {
        if (mOriginatingAddress == null) {
            return null;
        }

        return mOriginatingAddress.getAddressString();
    }

    /**
     * Returns the originating address, or email from address if this message
     * was from an email gateway. Returns null if originating address
     * unavailable.
     */
    public String getDisplayOriginatingAddress() {
        if (mIsEmail) {
            return mEmailFrom;
        } else {
            return getOriginatingAddress();
        }
    }

    /**
     * Returns the message body as a String, if it exists and is text based.
     * @return message body is there is one, otherwise null
     */
    public String getMessageBody() {
        return mMessageBody;
    }

    /**
     * Returns the class of this message.
     */
    public abstract SmsConstants.MessageClass getMessageClass();

    /**
     * Returns the message body, or email message body if this message was from
     * an email gateway. Returns null if message body unavailable.
     */
    public String getDisplayMessageBody() {
        if (mIsEmail) {
            return mEmailBody;
        } else {
            return getMessageBody();
        }
    }

    /**
     * Unofficial convention of a subject line enclosed in parens empty string
     * if not present
     */
    public String getPseudoSubject() {
        return mPseudoSubject == null ? "" : mPseudoSubject;
    }

    /**
     * Returns the service centre timestamp in currentTimeMillis() format
     */
    public long getTimestampMillis() {
        return mScTimeMillis;
    }

    /**
     * Returns true if message is an email.
     *
     * @return true if this message came through an email gateway and email
     *         sender / subject / parsed body are available
     */
    public boolean isEmail() {
        return mIsEmail;
    }

    /**
     * @return if isEmail() is true, body of the email sent through the gateway.
     *         null otherwise
     */
    public String getEmailBody() {
        return mEmailBody;
    }

    /**
     * @return if isEmail() is true, email from address of email sent through
     *         the gateway. null otherwise
     */
    public String getEmailFrom() {
        return mEmailFrom;
    }

    /**
     * Get protocol identifier.
     */
    public abstract int getProtocolIdentifier();

    /**
     * See TS 23.040 9.2.3.9 returns true if this is a "replace short message"
     * SMS
     */
    public abstract boolean isReplace();

    /**
     * Returns true for CPHS MWI toggle message.
     *
     * @return true if this is a CPHS MWI toggle message See CPHS 4.2 section
     *         B.4.2
     */
    public abstract boolean isCphsMwiMessage();

    /**
     * returns true if this message is a CPHS voicemail / message waiting
     * indicator (MWI) clear message
     */
    public abstract boolean isMWIClearMessage();

    /**
     * returns true if this message is a CPHS voicemail / message waiting
     * indicator (MWI) set message
     */
    public abstract boolean isMWISetMessage();

    /**
     * returns true if this message is a "Message Waiting Indication Group:
     * Discard Message" notification and should not be stored.
     */
    public abstract boolean isMwiDontStore();

    /**
     * returns the user data section minus the user data header if one was
     * present.
     */
    public byte[] getUserData() {
        return mUserData;
    }

    /**
     * Returns an object representing the user data header
     *
     * {@hide}
     */
    public SmsHeader getUserDataHeader() {
        return mUserDataHeader;
    }

    /**
     * TODO(cleanup): The term PDU is used in a seemingly non-unique
     * manner -- for example, what is the difference between this byte
     * array and the contents of SubmitPdu objects.  Maybe a more
     * illustrative term would be appropriate.
     */

    /**
     * Returns the raw PDU for the message.
     */
    public byte[] getPdu() {
        return mPdu;
    }

    /**
     * For an SMS-STATUS-REPORT message, this returns the status field from
     * the status report.  This field indicates the status of a previously
     * submitted SMS, if requested.  See TS 23.040, 9.2.3.15 TP-Status for a
     * description of values.
     *
     * @return 0 indicates the previously sent message was received.
     *         See TS 23.040, 9.9.2.3.15 for a description of other possible
     *         values.
     */
    public abstract int getStatus();

    /**
     * Return true iff the message is a SMS-STATUS-REPORT message.
     */
    public abstract boolean isStatusReportMessage();

    /**
     * Returns true iff the <code>TP-Reply-Path</code> bit is set in
     * this message.
     */
    public abstract boolean isReplyPathPresent();

    /**
     * Returns the status of the message on the ICC (read, unread, sent, unsent).
     *
     * @return the status of the message on the ICC.  These are:
     *         SmsManager.STATUS_ON_ICC_FREE
     *         SmsManager.STATUS_ON_ICC_READ
     *         SmsManager.STATUS_ON_ICC_UNREAD
     *         SmsManager.STATUS_ON_ICC_SEND
     *         SmsManager.STATUS_ON_ICC_UNSENT
     */
    public int getStatusOnIcc() {
        return mStatusOnIcc;
    }

    /**
     * Returns the record index of the message on the ICC (1-based index).
     * @return the record index of the message on the ICC, or -1 if this
     *         SmsMessage was not created from a ICC SMS EF record.
     */
    public int getIndexOnIcc() {
        return mIndexOnIcc;
    }

    protected void parseMessageBody() {
        // originatingAddress could be null if this message is from a status
        // report.
        if (mOriginatingAddress != null && mOriginatingAddress.couldBeEmailGateway()) {
            extractEmailAddressFromMessageBody();
        }
    }

    /**
     * Try to parse this message as an email gateway message
     * There are two ways specified in TS 23.040 Section 3.8 :
     *  - SMS message "may have its TP-PID set for Internet electronic mail - MT
     * SMS format: [<from-address><space>]<message> - "Depending on the
     * nature of the gateway, the destination/origination address is either
     * derived from the content of the SMS TP-OA or TP-DA field, or the
     * TP-OA/TP-DA field contains a generic gateway address and the to/from
     * address is added at the beginning as shown above." (which is supported here)
     * - Multiple addresses separated by commas, no spaces, Subject field delimited
     * by '()' or '##' and '#' Section 9.2.3.24.11 (which are NOT supported here)
     */
    protected void extractEmailAddressFromMessageBody() {

        /* Some carriers may use " /" delimiter as below
         *
         * 1. [x@y][ ]/[subject][ ]/[body]
         * -or-
         * 2. [x@y][ ]/[body]
         */
         String[] parts = mMessageBody.split("( /)|( )", 2);
         if (parts.length < 2) return;
         mEmailFrom = parts[0];
         mEmailBody = parts[1];
         mIsEmail = Telephony.Mms.isEmailAddress(mEmailFrom);
    }

    /**
     * Find the next position to start a new fragment of a multipart SMS.
     *
     * @param currentPosition current start position of the fragment
     * @param byteLimit maximum number of bytes in the fragment
     * @param msgBody text of the SMS in UTF-16 encoding
     * @return the position to start the next fragment
     */
    public static int findNextUnicodePosition(
            int currentPosition, int byteLimit, CharSequence msgBody) {
        int nextPos = Math.min(currentPosition + byteLimit / 2, msgBody.length());
        // Check whether the fragment ends in a character boundary. Some characters take 4-bytes
        // in UTF-16 encoding. Many carriers cannot handle
        // a fragment correctly if it does not end at a character boundary.
        if (nextPos < msgBody.length()) {
            BreakIterator breakIterator = BreakIterator.getCharacterInstance();
            breakIterator.setText(msgBody.toString());
            if (!breakIterator.isBoundary(nextPos)) {
                nextPos = breakIterator.preceding(nextPos);
            }
        }
        return nextPos;
    }

    /**
     * Calculate the TextEncodingDetails of a message encoded in Unicode.
     */
    public static TextEncodingDetails calcUnicodeEncodingDetails(CharSequence msgBody) {
        TextEncodingDetails ted = new TextEncodingDetails();
        int octets = msgBody.length() * 2;
        ted.codeUnitSize = SmsConstants.ENCODING_16BIT;
        ted.codeUnitCount = msgBody.length();
        if (octets > SmsConstants.MAX_USER_DATA_BYTES) {
            // If EMS is not supported, break down EMS into single segment SMS
            // and add page info " x/y".
            // In the case of UCS2 encoding type, we need 8 bytes for this
            // but we only have 6 bytes from UDH, so truncate the limit for
            // each segment by 2 bytes (1 char).
            int maxUserDataBytesWithHeader = SmsConstants.MAX_USER_DATA_BYTES_WITH_HEADER;
            if (!SmsMessage.hasEmsSupport()) {
                // make sure total number of segments is less than 10
                if (octets <= 9 * (maxUserDataBytesWithHeader - 2)) {
                    maxUserDataBytesWithHeader -= 2;
                }
            }

            int pos = 0;  // Index in code units.
            int msgCount = 0;
            while (pos < msgBody.length()) {
                int nextPos = findNextUnicodePosition(pos, maxUserDataBytesWithHeader,
                        msgBody);
                if (nextPos == msgBody.length()) {
                    ted.codeUnitsRemaining = pos + maxUserDataBytesWithHeader / 2 -
                            msgBody.length();
                }
                pos = nextPos;
                msgCount++;
            }
            ted.msgCount = msgCount;
        } else {
            ted.msgCount = 1;
            ted.codeUnitsRemaining = (SmsConstants.MAX_USER_DATA_BYTES - octets) / 2;
        }

        return ted;
    }
}
