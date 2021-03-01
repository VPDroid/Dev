/*
 * Copyright (C) 2013 The Android Open Source Project
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
package android.annotation;

import java.lang.annotation.Retention;
import java.lang.annotation.Target;

import static java.lang.annotation.ElementType.ANNOTATION_TYPE;
import static java.lang.annotation.RetentionPolicy.CLASS;

/**
 * Denotes that the annotated String element, represents a logical
 * type and that its value should be one of the explicitly named constants.
 * <p>
 * Example:
 * <pre>{@code
 *  &#64;Retention(SOURCE)
 *  &#64;StringDef(&#123;
 *     POWER_SERVICE,
 *     WINDOW_SERVICE,
 *     LAYOUT_INFLATER_SERVICE
 *  &#125;)
 *  public &#64;interface ServiceName &#123;&#125;
 *  public static final String POWER_SERVICE = "power";
 *  public static final String WINDOW_SERVICE = "window";
 *  public static final String LAYOUT_INFLATER_SERVICE = "layout_inflater";
 *  ...
 *  public abstract Object getSystemService(&#64;ServiceName String name);
 * }</pre>
 *
 * @hide
 */
@Retention(CLASS)
@Target({ANNOTATION_TYPE})
public @interface StringDef {
    /** Defines the allowed constants for this element */
    String[] value() default {};
}
