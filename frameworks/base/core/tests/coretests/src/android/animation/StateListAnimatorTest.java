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


package android.animation;

import android.test.ActivityInstrumentationTestCase2;
import android.test.UiThreadTest;
import android.util.StateSet;
import android.view.View;
import android.view.ViewGroup;

import com.android.frameworks.coretests.R;

import java.util.concurrent.atomic.AtomicInteger;


public class StateListAnimatorTest extends ActivityInstrumentationTestCase2<BasicAnimatorActivity> {

    public StateListAnimatorTest() {
        super(BasicAnimatorActivity.class);
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
    }

    public void testInflateFromAnimator() throws Exception {
        StateListAnimator stateListAnimator = AnimatorInflater
                .loadStateListAnimator(getActivity(), R.anim.test_state_anim);
        assertNotNull("A state list animator should be returned", stateListAnimator);
        assertEquals("State list animator should have three items", 3,
                stateListAnimator.getTuples().size());
    }

    @UiThreadTest
    public void testAttachDetach() throws Exception {
        View view = new View(getActivity());
        final AtomicInteger setStateCount = new AtomicInteger(0);
        StateListAnimator stateListAnimator = new StateListAnimator() {
            @Override
            public void setState(int[] state) {
                setStateCount.incrementAndGet();
                super.setState(state);
            }
        };
        view.setStateListAnimator(stateListAnimator);
        assertNotNull("State list animator should have a reference to view even if it is detached",
                stateListAnimator.getTarget());
        ViewGroup viewGroup = (ViewGroup) getActivity().findViewById(android.R.id.content);
        int preSetStateCount = setStateCount.get();
        viewGroup.addView(view);
        assertTrue("When view is attached, state list drawable's setState should be called",
                preSetStateCount < setStateCount.get());

        StateListAnimator stateListAnimator2 = new StateListAnimator();
        view.setStateListAnimator(stateListAnimator2);
        assertNull("When a new state list animator is assigned, previous one should be detached",
                stateListAnimator.getTarget());
        assertNull("Any running animator should be removed on detach",
                stateListAnimator.getRunningAnimator());
        assertEquals("The new state list animator should be attached to the view",
                view, stateListAnimator2.getTarget());
        viewGroup.removeView(view);
        assertNotNull("When view is detached from window, state list animator should still keep the"
                        + " reference",
                stateListAnimator2.getTarget());
    }

    public void testStateListLoading() throws InterruptedException {
        StateListAnimator stateListAnimator = AnimatorInflater
                .loadStateListAnimator(getActivity(), R.anim.test_state_anim);
        assertNotNull("A state list animator should be returned", stateListAnimator);
        assertEquals("Steate list animator should have two items", 3,
                stateListAnimator.getTuples().size());
        StateListAnimator.Tuple tuple1 = stateListAnimator.getTuples().get(0);
        assertEquals("first tuple should have one state", 1, tuple1.getSpecs().length);
        assertEquals("first spec in tuple 1 should be pressed",
                com.android.internal.R.attr.state_pressed, tuple1.getSpecs()[0]);

        StateListAnimator.Tuple tuple2 = stateListAnimator.getTuples().get(1);
        assertEquals("Second tuple should have two specs", 2, tuple2.getSpecs().length);
        assertTrue("Tuple two should match the expected state",
                StateSet.stateSetMatches(tuple2.getSpecs(),
                new int[]{-com.android.internal.R.attr.state_pressed,
                com.android.internal.R.attr.state_enabled}));
    }
}
