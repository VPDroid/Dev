/*
 * Copyright (C) 2009 The Android Open Source Project
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
package com.android.vcard.tests.testutils;

import android.test.AndroidTestCase;

import com.android.vcard.VCardParser;
import com.android.vcard.VCardUtils;
import com.android.vcard.exception.VCardException;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

public class PropertyNodesVerifier extends VNodeBuilder {
    private final List<PropertyNodesVerifierElem> mPropertyNodesVerifierElemList;
    private final AndroidTestCase mAndroidTestCase;
    private int mIndex;

    public PropertyNodesVerifier(AndroidTestCase testCase) {
        super();
        mPropertyNodesVerifierElemList = new ArrayList<PropertyNodesVerifierElem>();
        mAndroidTestCase = testCase;
    }

    public PropertyNodesVerifierElem addPropertyNodesVerifierElem() {
        PropertyNodesVerifierElem elem = new PropertyNodesVerifierElem(mAndroidTestCase);
        mPropertyNodesVerifierElemList.add(elem);
        return elem;
    }

    public void verify(int resId, int vcardType) throws IOException, VCardException {
        verify(mAndroidTestCase.getContext().getResources().openRawResource(resId), vcardType);
    }

    public void verify(int resId, int vcardType, final VCardParser parser)
            throws IOException, VCardException {
        verify(mAndroidTestCase.getContext().getResources().openRawResource(resId),
                vcardType, parser);
    }

    public void verify(InputStream is, int vcardType) throws IOException, VCardException {
        final VCardParser parser = VCardUtils.getAppropriateParser(vcardType);
        verify(is, vcardType, parser);
    }

    public void verify(InputStream is, int vcardType, final VCardParser parser)
            throws IOException, VCardException {
        parser.addInterpreter(this);
        try {
            parser.parse(is);
        } finally {
            if (is != null) {
                try {
                    is.close();
                } catch (IOException e) {
                }
            }
        }
    }

    @Override
    public void onEntryStarted() {
        super.onEntryStarted();
        AndroidTestCase.assertTrue(mIndex < mPropertyNodesVerifierElemList.size());
    }

    @Override
    public void onEntryEnded() {
        mPropertyNodesVerifierElemList.get(mIndex).verify(getCurrentVNode());
        super.onEntryEnded();
        mIndex++;
    }
}
