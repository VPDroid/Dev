/*
 * Copyright (C) 2006 The Android Open Source Project
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

import junit.framework.TestCase;
import android.test.suitebuilder.annotation.SmallTest;

import com.android.internal.telephony.uicc.AdnRecord;
import com.android.internal.telephony.uicc.IccUtils;

/**
 * {@hide}
 */
public class AdnRecordTest extends TestCase {
    
    @SmallTest
    public void testBasic() throws Exception {
        AdnRecord adn;

        //
        // Typical record
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes("566F696365204D61696C07918150367742F3FFFFFFFFFFFF"));

        assertEquals("Voice Mail", adn.getAlphaTag());
        assertEquals("+18056377243", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // Empty records, empty strings
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"));

        assertEquals("", adn.getAlphaTag());
        assertEquals("", adn.getNumber());
        assertTrue(adn.isEmpty());

        //
        // Record too short
        // 
        adn = new AdnRecord(IccUtils.hexStringToBytes( "FF"));

        assertEquals("", adn.getAlphaTag());
        assertEquals("", adn.getNumber());
        assertTrue(adn.isEmpty());

        //
        // TOA = 0xff ("control string")
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes("566F696365204D61696C07FF8150367742F3FFFFFFFFFFFF"));

        assertEquals("Voice Mail", adn.getAlphaTag());
        assertEquals("18056377243", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // TOA = 0x81 (unknown)
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes("566F696365204D61696C07818150367742F3FFFFFFFFFFFF"));

        assertEquals("Voice Mail", adn.getAlphaTag());
        assertEquals("18056377243", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // Number Length is too long
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes("566F696365204D61696C0F918150367742F3FFFFFFFFFFFF"));

        assertEquals("Voice Mail", adn.getAlphaTag());
        assertEquals("", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // Number Length is zero (invalid)
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes("566F696365204D61696C00918150367742F3FFFFFFFFFFFF"));

        assertEquals("Voice Mail", adn.getAlphaTag());
        assertEquals("", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // Number Length is 2, first number byte is FF, TOA is international
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes("566F696365204D61696C0291FF50367742F3FFFFFFFFFFFF"));

        assertEquals("Voice Mail", adn.getAlphaTag());
        assertEquals("", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // Number Length is 2, first number digit is valid, TOA is international
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes("566F696365204D61696C0291F150367742F3FFFFFFFFFFFF"));

        assertEquals("Voice Mail", adn.getAlphaTag());
        assertEquals("+1", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // An extended record
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes(
                        "4164676A6DFFFFFFFFFFFFFFFFFFFFFF0B918188551512C221436587FF01"));

        assertEquals("Adgjm", adn.getAlphaTag());
        assertEquals("+18885551212,12345678", adn.getNumber());
        assertFalse(adn.isEmpty());
        assertTrue(adn.hasExtendedRecord());

        adn.appendExtRecord(IccUtils.hexStringToBytes("0206092143658709ffffffffff"));

        assertEquals("Adgjm", adn.getAlphaTag());
        assertEquals("+18885551212,12345678901234567890", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // An extended record with an invalid extension
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes(
                        "4164676A6DFFFFFFFFFFFFFFFFFFFFFF0B918188551512C221436587FF01"));

        assertEquals("Adgjm", adn.getAlphaTag());
        assertEquals("+18885551212,12345678", adn.getNumber());
        assertFalse(adn.isEmpty());
        assertTrue(adn.hasExtendedRecord());

        adn.appendExtRecord(IccUtils.hexStringToBytes("0106092143658709ffffffffff"));

        assertEquals("Adgjm", adn.getAlphaTag());
        assertEquals("+18885551212,12345678", adn.getNumber());
        assertFalse(adn.isEmpty());

        //
        // An extended record with an invalid extension
        // 
        adn = new AdnRecord(
                IccUtils.hexStringToBytes(
                        "4164676A6DFFFFFFFFFFFFFFFFFFFFFF0B918188551512C221436587FF01"));

        assertEquals("Adgjm", adn.getAlphaTag());
        assertEquals("+18885551212,12345678", adn.getNumber());
        assertFalse(adn.isEmpty());
        assertTrue(adn.hasExtendedRecord());

        adn.appendExtRecord(IccUtils.hexStringToBytes("020B092143658709ffffffffff"));

        assertEquals("Adgjm", adn.getAlphaTag());
        assertEquals("+18885551212,12345678", adn.getNumber());
        assertFalse(adn.isEmpty());
    }
}


