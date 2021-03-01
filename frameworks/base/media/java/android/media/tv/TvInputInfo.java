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

package android.media.tv;

import android.annotation.NonNull;
import android.annotation.SystemApi;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.content.res.Resources;
import android.content.res.TypedArray;
import android.content.res.XmlResourceParser;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.Icon;
import android.hardware.hdmi.HdmiDeviceInfo;
import android.net.Uri;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.UserHandle;
import android.provider.Settings;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.util.Log;
import android.util.SparseIntArray;
import android.util.Xml;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * This class is used to specify meta information of a TV input.
 */
public final class TvInputInfo implements Parcelable {
    private static final boolean DEBUG = false;
    private static final String TAG = "TvInputInfo";

    // Should be in sync with frameworks/base/core/res/res/values/attrs.xml
    /**
     * TV input type: the TV input service is a tuner which provides channels.
     */
    public static final int TYPE_TUNER = 0;
    /**
     * TV input type: a generic hardware TV input type.
     */
    public static final int TYPE_OTHER = 1000;
    /**
     * TV input type: the TV input service represents a composite port.
     */
    public static final int TYPE_COMPOSITE = 1001;
    /**
     * TV input type: the TV input service represents a SVIDEO port.
     */
    public static final int TYPE_SVIDEO = 1002;
    /**
     * TV input type: the TV input service represents a SCART port.
     */
    public static final int TYPE_SCART = 1003;
    /**
     * TV input type: the TV input service represents a component port.
     */
    public static final int TYPE_COMPONENT = 1004;
    /**
     * TV input type: the TV input service represents a VGA port.
     */
    public static final int TYPE_VGA = 1005;
    /**
     * TV input type: the TV input service represents a DVI port.
     */
    public static final int TYPE_DVI = 1006;
    /**
     * TV input type: the TV input service is HDMI. (e.g. HDMI 1)
     */
    public static final int TYPE_HDMI = 1007;
    /**
     * TV input type: the TV input service represents a display port.
     */
    public static final int TYPE_DISPLAY_PORT = 1008;

    /**
     * The ID of the TV input to provide to the setup activity and settings activity.
     */
    public static final String EXTRA_INPUT_ID = "android.media.tv.extra.INPUT_ID";

    private static final SparseIntArray sHardwareTypeToTvInputType = new SparseIntArray();

    private static final String XML_START_TAG_NAME = "tv-input";
    private static final String DELIMITER_INFO_IN_ID = "/";
    private static final String PREFIX_HDMI_DEVICE = "HDMI";
    private static final String PREFIX_HARDWARE_DEVICE = "HW";
    private static final int LENGTH_HDMI_PHYSICAL_ADDRESS = 4;
    private static final int LENGTH_HDMI_DEVICE_ID = 2;

    private final ResolveInfo mService;
    private final String mId;
    private final String mParentId;
    private final int mType;
    private final boolean mIsHardwareInput;

    // Attributes from XML meta data.
    private String mSetupActivity;
    private String mSettingsActivity;

    private HdmiDeviceInfo mHdmiDeviceInfo;
    private int mLabelRes;
    private String mLabel;
    private Icon mIcon;
    private Uri mIconUri;
    private boolean mIsConnectedToHdmiSwitch;

    static {
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_OTHER_HARDWARE,
                TYPE_OTHER);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_TUNER, TYPE_TUNER);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_COMPOSITE, TYPE_COMPOSITE);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_SVIDEO, TYPE_SVIDEO);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_SCART, TYPE_SCART);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_COMPONENT, TYPE_COMPONENT);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_VGA, TYPE_VGA);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_DVI, TYPE_DVI);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_HDMI, TYPE_HDMI);
        sHardwareTypeToTvInputType.put(TvInputHardwareInfo.TV_INPUT_TYPE_DISPLAY_PORT,
                TYPE_DISPLAY_PORT);
    }

    /**
     * Create a new instance of the TvInputInfo class,
     * instantiating it from the given Context and ResolveInfo.
     *
     * @param service The ResolveInfo returned from the package manager about this TV input service.
     * @hide
     */
    public static TvInputInfo createTvInputInfo(Context context, ResolveInfo service)
            throws XmlPullParserException, IOException {
        return createTvInputInfo(context, service, generateInputIdForComponentName(
                new ComponentName(service.serviceInfo.packageName, service.serviceInfo.name)),
                null, TYPE_TUNER, false, 0, null, null, null, false);
    }

    /**
     * Create a new instance of the TvInputInfo class, instantiating it from the given Context,
     * ResolveInfo, and HdmiDeviceInfo.
     *
     * @param service The ResolveInfo returned from the package manager about this TV input service.
     * @param hdmiDeviceInfo The HdmiDeviceInfo for a HDMI CEC logical device.
     * @param parentId The ID of this TV input's parent input. {@code null} if none exists.
     * @param label The label of this TvInputInfo. If it is {@code null} or empty, {@code service}
     *            label will be loaded.
     * @param iconUri The {@link android.net.Uri} to load the icon image. See
     *            {@link android.content.ContentResolver#openInputStream}. If it is {@code null},
     *            the application icon of {@code service} will be loaded.
     * @hide
     */
    @SystemApi
    public static TvInputInfo createTvInputInfo(Context context, ResolveInfo service,
            HdmiDeviceInfo hdmiDeviceInfo, String parentId, String label, Uri iconUri)
                    throws XmlPullParserException, IOException {
        boolean isConnectedToHdmiSwitch = (hdmiDeviceInfo.getPhysicalAddress() & 0x0FFF) != 0;
        TvInputInfo input = createTvInputInfo(context, service, generateInputIdForHdmiDevice(
                new ComponentName(service.serviceInfo.packageName, service.serviceInfo.name),
                hdmiDeviceInfo), parentId, TYPE_HDMI, true, 0, label, null, iconUri,
                isConnectedToHdmiSwitch);
        input.mHdmiDeviceInfo = hdmiDeviceInfo;
        return input;
    }

    /**
     * Create a new instance of the TvInputInfo class, instantiating it from the given Context,
     * ResolveInfo, and HdmiDeviceInfo.
     *
     * @param service The ResolveInfo returned from the package manager about this TV input service.
     * @param hdmiDeviceInfo The HdmiDeviceInfo for a HDMI CEC logical device.
     * @param parentId The ID of this TV input's parent input. {@code null} if none exists.
     * @param labelRes The label resource ID of this TvInputInfo. If it is {@code 0},
     *            {@code service} label will be loaded.
     * @param icon The {@link android.graphics.drawable.Icon} to load the icon image. If it is
     *            {@code null}, the application icon of {@code service} will be loaded.
     * @hide
     */
    @SystemApi
    public static TvInputInfo createTvInputInfo(Context context, ResolveInfo service,
            HdmiDeviceInfo hdmiDeviceInfo, String parentId, int labelRes, Icon icon)
            throws XmlPullParserException, IOException {
        boolean isConnectedToHdmiSwitch = (hdmiDeviceInfo.getPhysicalAddress() & 0x0FFF) != 0;
        TvInputInfo input = createTvInputInfo(context, service, generateInputIdForHdmiDevice(
                new ComponentName(service.serviceInfo.packageName, service.serviceInfo.name),
                hdmiDeviceInfo), parentId, TYPE_HDMI, true, labelRes, null, icon, null,
                isConnectedToHdmiSwitch);
        input.mHdmiDeviceInfo = hdmiDeviceInfo;
        return input;
    }

    /**
     * Create a new instance of the TvInputInfo class, instantiating it from the given Context,
     * ResolveInfo, and TvInputHardwareInfo.
     *
     * @param service The ResolveInfo returned from the package manager about this TV input service.
     * @param hardwareInfo The TvInputHardwareInfo for a TV input hardware device.
     * @param label The label of this TvInputInfo. If it is {@code null} or empty, {@code service}
     *            label will be loaded.
     * @param iconUri The {@link android.net.Uri} to load the icon image. See
     *            {@link android.content.ContentResolver#openInputStream}. If it is {@code null},
     *            the application icon of {@code service} will be loaded.
     * @hide
     */
    @SystemApi
    public static TvInputInfo createTvInputInfo(Context context, ResolveInfo service,
            TvInputHardwareInfo hardwareInfo, String label, Uri iconUri)
                    throws XmlPullParserException, IOException {
        int inputType = sHardwareTypeToTvInputType.get(hardwareInfo.getType(), TYPE_TUNER);
        return createTvInputInfo(context, service, generateInputIdForHardware(
                new ComponentName(service.serviceInfo.packageName, service.serviceInfo.name),
                hardwareInfo), null, inputType, true, 0, label, null, iconUri, false);
    }

    /**
     * Create a new instance of the TvInputInfo class, instantiating it from the given Context,
     * ResolveInfo, and TvInputHardwareInfo.
     *
     * @param service The ResolveInfo returned from the package manager about this TV input service.
     * @param hardwareInfo The TvInputHardwareInfo for a TV input hardware device.
     * @param labelRes The label resource ID of this TvInputInfo. If it is {@code 0},
     *            {@code service} label will be loaded.
     * @param icon The {@link android.graphics.drawable.Icon} to load the icon image. If it is
     *            {@code null}, the application icon of {@code service} will be loaded.
     * @hide
     */
    @SystemApi
    public static TvInputInfo createTvInputInfo(Context context, ResolveInfo service,
            TvInputHardwareInfo hardwareInfo, int labelRes, Icon icon)
            throws XmlPullParserException, IOException {
        int inputType = sHardwareTypeToTvInputType.get(hardwareInfo.getType(), TYPE_TUNER);
        return createTvInputInfo(context, service, generateInputIdForHardware(
                new ComponentName(service.serviceInfo.packageName, service.serviceInfo.name),
                hardwareInfo), null, inputType, true, labelRes, null, icon, null, false);
    }

    private static TvInputInfo createTvInputInfo(Context context, ResolveInfo service, String id,
            String parentId, int inputType, boolean isHardwareInput, int labelRes, String label,
            Icon icon, Uri iconUri, boolean isConnectedToHdmiSwitch)
                    throws XmlPullParserException, IOException {
        ServiceInfo si = service.serviceInfo;
        PackageManager pm = context.getPackageManager();
        XmlResourceParser parser = null;
        try {
            parser = si.loadXmlMetaData(pm, TvInputService.SERVICE_META_DATA);
            if (parser == null) {
                throw new XmlPullParserException("No " + TvInputService.SERVICE_META_DATA
                        + " meta-data for " + si.name);
            }

            Resources res = pm.getResourcesForApplication(si.applicationInfo);
            AttributeSet attrs = Xml.asAttributeSet(parser);

            int type;
            while ((type=parser.next()) != XmlPullParser.END_DOCUMENT
                    && type != XmlPullParser.START_TAG) {
            }

            String nodeName = parser.getName();
            if (!XML_START_TAG_NAME.equals(nodeName)) {
                throw new XmlPullParserException(
                        "Meta-data does not start with tv-input-service tag in " + si.name);
            }

            TvInputInfo input = new TvInputInfo(service, id, parentId, inputType, isHardwareInput);
            TypedArray sa = res.obtainAttributes(attrs,
                    com.android.internal.R.styleable.TvInputService);
            input.mSetupActivity = sa.getString(
                    com.android.internal.R.styleable.TvInputService_setupActivity);
            if (DEBUG) {
                Log.d(TAG, "Setup activity loaded. [" + input.mSetupActivity + "] for " + si.name);
            }
            if (inputType == TYPE_TUNER && TextUtils.isEmpty(input.mSetupActivity)) {
                throw new XmlPullParserException("Setup activity not found in " + si.name);
            }
            input.mSettingsActivity = sa.getString(
                    com.android.internal.R.styleable.TvInputService_settingsActivity);
            if (DEBUG) {
                Log.d(TAG, "Settings activity loaded. [" + input.mSettingsActivity + "] for "
                        + si.name);
            }
            sa.recycle();

            input.mLabelRes = labelRes;
            input.mLabel = label;
            input.mIcon = icon;
            input.mIconUri = iconUri;
            input.mIsConnectedToHdmiSwitch = isConnectedToHdmiSwitch;
            return input;
        } catch (NameNotFoundException e) {
            throw new XmlPullParserException("Unable to create context for: " + si.packageName);
        } finally {
            if (parser != null) {
                parser.close();
            }
        }
    }

    /**
     * Constructor.
     *
     * @param service The ResolveInfo returned from the package manager about this TV input service.
     * @param id ID of this TV input. Should be generated via generateInputId*().
     * @param parentId ID of this TV input's parent input. {@code null} if none exists.
     * @param type The type of this TV input service.
     * @param isHardwareInput {@code true} if this TV input represents a hardware device.
     *         {@code false} otherwise.
     */
    private TvInputInfo(ResolveInfo service, String id, String parentId, int type,
            boolean isHardwareInput) {
        mService = service;
        mId = id;
        mParentId = parentId;
        mType = type;
        mIsHardwareInput = isHardwareInput;
    }

    /**
     * Returns a unique ID for this TV input. The ID is generated from the package and class name
     * implementing the TV input service.
     */
    public String getId() {
        return mId;
    }

    /**
     * Returns the parent input ID.
     *
     * <p>A TV input may have a parent input if the TV input is actually a logical representation of
     * a device behind the hardware port represented by the parent input.
     * For example, a HDMI CEC logical device, connected to a HDMI port, appears as another TV
     * input. In this case, the parent input of this logical device is the HDMI port.
     *
     * <p>Applications may group inputs by parent input ID to provide an easier access to inputs
     * sharing the same physical port. In the example of HDMI CEC, logical HDMI CEC devices behind
     * the same HDMI port have the same parent ID, which is the ID representing the port. Thus
     * applications can group the hardware HDMI port and the logical HDMI CEC devices behind it
     * together using this method.
     *
     * @return the ID of the parent input, if exists. Returns {@code null} if the parent input is
     *         not specified.
     */
    public String getParentId() {
        return mParentId;
    }

    /**
     * Returns the information of the service that implements this TV input.
     */
    public ServiceInfo getServiceInfo() {
        return mService.serviceInfo;
    }

    /**
     * Returns the component of the service that implements this TV input.
     * @hide
     */
    public ComponentName getComponent() {
        return new ComponentName(mService.serviceInfo.packageName, mService.serviceInfo.name);
    }

    /**
     * Returns an intent to start the setup activity for this TV input.
     */
    public Intent createSetupIntent() {
        if (!TextUtils.isEmpty(mSetupActivity)) {
            Intent intent = new Intent(Intent.ACTION_MAIN);
            intent.setClassName(mService.serviceInfo.packageName, mSetupActivity);
            intent.putExtra(EXTRA_INPUT_ID, getId());
            return intent;
        }
        return null;
    }

    /**
     * Returns an intent to start the settings activity for this TV input.
     */
    public Intent createSettingsIntent() {
        if (!TextUtils.isEmpty(mSettingsActivity)) {
            Intent intent = new Intent(Intent.ACTION_MAIN);
            intent.setClassName(mService.serviceInfo.packageName, mSettingsActivity);
            intent.putExtra(EXTRA_INPUT_ID, getId());
            return intent;
        }
        return null;
    }

    /**
     * Returns the type of this TV input.
     */
    public int getType() {
        return mType;
    }

    /**
     * Returns the HDMI device information of this TV input.
     * @hide
     */
    @SystemApi
    public HdmiDeviceInfo getHdmiDeviceInfo() {
        if (mType == TYPE_HDMI) {
            return mHdmiDeviceInfo;
        }
        return null;
    }

    /**
     * Returns {@code true} if this TV input is pass-though which does not have any real channels in
     * TvProvider. {@code false} otherwise.
     *
     * @see TvContract#buildChannelUriForPassthroughInput(String)
     */
    public boolean isPassthroughInput() {
        return mType != TYPE_TUNER;
    }

    /**
     * Returns {@code true} if this TV input represents a hardware device. (e.g. built-in tuner,
     * HDMI1) {@code false} otherwise.
     * @hide
     */
    @SystemApi
    public boolean isHardwareInput() {
        return mIsHardwareInput;
    }

    /**
     * Returns {@code true}, if a CEC device for this TV input is connected to an HDMI switch, i.e.,
     * the device isn't directly connected to a HDMI port.
     * @hide
     */
    @SystemApi
    public boolean isConnectedToHdmiSwitch() {
        return mIsConnectedToHdmiSwitch;
    }

    /**
     * Checks if this TV input is marked hidden by the user in the settings.
     *
     * @param context Supplies a {@link Context} used to check if this TV input is hidden.
     * @return {@code true} if the user marked this TV input hidden in settings. {@code false}
     *         otherwise.
     * @hide
     */
    @SystemApi
    public boolean isHidden(Context context) {
        return TvInputSettings.isHidden(context, mId, UserHandle.myUserId());
    }

    /**
     * Loads the user-displayed label for this TV input.
     *
     * @param context Supplies a {@link Context} used to load the label.
     * @return a CharSequence containing the TV input's label. If the TV input does not have
     *         a label, its name is returned.
     */
    public CharSequence loadLabel(@NonNull Context context) {
        if (mLabelRes != 0) {
            return context.getPackageManager().getText(mService.serviceInfo.packageName, mLabelRes,
                    null);
        } else if (!TextUtils.isEmpty(mLabel)) {
            return mLabel;
        }
        return mService.loadLabel(context.getPackageManager());
    }

    /**
     * Loads the custom label set by user in settings.
     *
     * @param context Supplies a {@link Context} used to load the custom label.
     * @return a CharSequence containing the TV input's custom label. {@code null} if there is no
     *         custom label.
     * @hide
     */
    @SystemApi
    public CharSequence loadCustomLabel(Context context) {
        return TvInputSettings.getCustomLabel(context, mId, UserHandle.myUserId());
    }

    /**
     * Loads the user-displayed icon for this TV input.
     *
     * @param context Supplies a {@link Context} used to load the icon.
     * @return a Drawable containing the TV input's icon. If the TV input does not have an icon,
     *         application's icon is returned. If it's unavailable too, {@code null} is returned.
     */
    public Drawable loadIcon(@NonNull Context context) {
        if (mIcon != null) {
            return mIcon.loadDrawable(context);
        } else if (mIconUri != null) {
            try (InputStream is = context.getContentResolver().openInputStream(mIconUri)) {
                Drawable drawable = Drawable.createFromStream(is, null);
                if (drawable != null) {
                    return drawable;
                }
            } catch (IOException e) {
                Log.w(TAG, "Loading the default icon due to a failure on loading " + mIconUri, e);
                // Falls back.
            }
        }
        return loadServiceIcon(context);
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public int hashCode() {
        return mId.hashCode();
    }

    @Override
    public boolean equals(Object o) {
        if (o == this) {
            return true;
        }

        if (!(o instanceof TvInputInfo)) {
            return false;
        }

        TvInputInfo obj = (TvInputInfo) o;
        return mId.equals(obj.mId);
    }

    @Override
    public String toString() {
        return "TvInputInfo{id=" + mId
                + ", pkg=" + mService.serviceInfo.packageName
                + ", service=" + mService.serviceInfo.name + "}";
    }

    /**
     * Used to package this object into a {@link Parcel}.
     *
     * @param dest The {@link Parcel} to be written.
     * @param flags The flags used for parceling.
     */
    @Override
    public void writeToParcel(@NonNull Parcel dest, int flags) {
        dest.writeString(mId);
        dest.writeString(mParentId);
        mService.writeToParcel(dest, flags);
        dest.writeString(mSetupActivity);
        dest.writeString(mSettingsActivity);
        dest.writeInt(mType);
        dest.writeByte(mIsHardwareInput ? (byte) 1 : 0);
        dest.writeParcelable(mHdmiDeviceInfo, flags);
        dest.writeParcelable(mIcon, flags);
        dest.writeParcelable(mIconUri, flags);
        dest.writeInt(mLabelRes);
        dest.writeString(mLabel);
        dest.writeByte(mIsConnectedToHdmiSwitch ? (byte) 1 : 0);
    }

    private Drawable loadServiceIcon(Context context) {
        if (mService.serviceInfo.icon == 0
                && mService.serviceInfo.applicationInfo.icon == 0) {
            return null;
        }
        return mService.serviceInfo.loadIcon(context.getPackageManager());
    }

    /**
     * Used to generate an input id from a ComponentName.
     *
     * @param name the component name for generating an input id.
     * @return the generated input id for the given {@code name}.
     */
    private static String generateInputIdForComponentName(ComponentName name) {
        return name.flattenToShortString();
    }

    /**
     * Used to generate an input id from a ComponentName and HdmiDeviceInfo.
     *
     * @param name the component name for generating an input id.
     * @param deviceInfo HdmiDeviceInfo describing this TV input.
     * @return the generated input id for the given {@code name} and {@code deviceInfo}.
     */
    private static String generateInputIdForHdmiDevice(
            ComponentName name, HdmiDeviceInfo deviceInfo) {
        // Example of the format : "/HDMI%04X%02X"
        String format = DELIMITER_INFO_IN_ID + PREFIX_HDMI_DEVICE
                + "%0" + LENGTH_HDMI_PHYSICAL_ADDRESS + "X"
                + "%0" + LENGTH_HDMI_DEVICE_ID + "X";
        return name.flattenToShortString() + String.format(Locale.ENGLISH, format,
                deviceInfo.getPhysicalAddress(), deviceInfo.getId());
    }

    /**
     * Used to generate an input id from a ComponentName and TvInputHardwareInfo
     *
     * @param name the component name for generating an input id.
     * @param hardwareInfo TvInputHardwareInfo describing this TV input.
     * @return the generated input id for the given {@code name} and {@code hardwareInfo}.
     */
    private static String generateInputIdForHardware(
            ComponentName name, TvInputHardwareInfo hardwareInfo) {
        return name.flattenToShortString() + DELIMITER_INFO_IN_ID + PREFIX_HARDWARE_DEVICE
                + hardwareInfo.getDeviceId();
    }

    public static final Parcelable.Creator<TvInputInfo> CREATOR =
            new Parcelable.Creator<TvInputInfo>() {
        @Override
        public TvInputInfo createFromParcel(Parcel in) {
            return new TvInputInfo(in);
        }

        @Override
        public TvInputInfo[] newArray(int size) {
            return new TvInputInfo[size];
        }
    };

    private TvInputInfo(Parcel in) {
        mId = in.readString();
        mParentId = in.readString();
        mService = ResolveInfo.CREATOR.createFromParcel(in);
        mSetupActivity = in.readString();
        mSettingsActivity = in.readString();
        mType = in.readInt();
        mIsHardwareInput = in.readByte() == 1;
        mHdmiDeviceInfo = in.readParcelable(null);
        mIcon = in.readParcelable(null);
        mIconUri = in.readParcelable(null);
        mLabelRes = in.readInt();
        mLabel = in.readString();
        mIsConnectedToHdmiSwitch = in.readByte() == 1;
    }

    /**
     * Utility class for putting and getting settings for TV input.
     *
     * @hide
     */
    @SystemApi
    public static final class TvInputSettings {
        private static final String TV_INPUT_SEPARATOR = ":";
        private static final String CUSTOM_NAME_SEPARATOR = ",";

        private TvInputSettings() { }

        private static boolean isHidden(Context context, String inputId, int userId) {
            return getHiddenTvInputIds(context, userId).contains(inputId);
        }

        private static String getCustomLabel(Context context, String inputId, int userId) {
            return getCustomLabels(context, userId).get(inputId);
        }

        /**
         * Returns a set of TV input IDs which are marked as hidden by user in the settings.
         *
         * @param context The application context
         * @param userId The user ID for the stored hidden input set
         * @hide
         */
        @SystemApi
        public static Set<String> getHiddenTvInputIds(Context context, int userId) {
            String hiddenIdsString = Settings.Secure.getStringForUser(
                    context.getContentResolver(), Settings.Secure.TV_INPUT_HIDDEN_INPUTS, userId);
            Set<String> set = new HashSet<>();
            if (TextUtils.isEmpty(hiddenIdsString)) {
                return set;
            }
            String[] ids = hiddenIdsString.split(TV_INPUT_SEPARATOR);
            for (String id : ids) {
                set.add(Uri.decode(id));
            }
            return set;
        }

        /**
         * Returns a map of TV input ID/custom label pairs set by the user in the settings.
         *
         * @param context The application context
         * @param userId The user ID for the stored hidden input map
         * @hide
         */
        @SystemApi
        public static Map<String, String> getCustomLabels(Context context, int userId) {
            String labelsString = Settings.Secure.getStringForUser(
                    context.getContentResolver(), Settings.Secure.TV_INPUT_CUSTOM_LABELS, userId);
            Map<String, String> map = new HashMap<>();
            if (TextUtils.isEmpty(labelsString)) {
                return map;
            }
            String[] pairs = labelsString.split(TV_INPUT_SEPARATOR);
            for (String pairString : pairs) {
                String[] pair = pairString.split(CUSTOM_NAME_SEPARATOR);
                map.put(Uri.decode(pair[0]), Uri.decode(pair[1]));
            }
            return map;
        }

        /**
         * Stores a set of TV input IDs which are marked as hidden by user. This is expected to
         * be called from the settings app.
         *
         * @param context The application context
         * @param hiddenInputIds A set including all the hidden TV input IDs
         * @param userId The user ID for the stored hidden input set
         * @hide
         */
        @SystemApi
        public static void putHiddenTvInputs(Context context, Set<String> hiddenInputIds,
                int userId) {
            StringBuilder builder = new StringBuilder();
            boolean firstItem = true;
            for (String inputId : hiddenInputIds) {
                ensureValidField(inputId);
                if (firstItem) {
                    firstItem = false;
                } else {
                    builder.append(TV_INPUT_SEPARATOR);
                }
                builder.append(Uri.encode(inputId));
            }
            Settings.Secure.putStringForUser(context.getContentResolver(),
                    Settings.Secure.TV_INPUT_HIDDEN_INPUTS, builder.toString(), userId);
        }

        /**
         * Stores a map of TV input ID/custom label set by user. This is expected to be
         * called from the settings app.
         *
         * @param context The application context.
         * @param customLabels A map of TV input ID/custom label pairs
         * @param userId The user ID for the stored hidden input map
         * @hide
         */
        @SystemApi
        public static void putCustomLabels(Context context,
                Map<String, String> customLabels, int userId) {
            StringBuilder builder = new StringBuilder();
            boolean firstItem = true;
            for (Map.Entry<String, String> entry: customLabels.entrySet()) {
                ensureValidField(entry.getKey());
                ensureValidField(entry.getValue());
                if (firstItem) {
                    firstItem = false;
                } else {
                    builder.append(TV_INPUT_SEPARATOR);
                }
                builder.append(Uri.encode(entry.getKey()));
                builder.append(CUSTOM_NAME_SEPARATOR);
                builder.append(Uri.encode(entry.getValue()));
            }
            Settings.Secure.putStringForUser(context.getContentResolver(),
                    Settings.Secure.TV_INPUT_CUSTOM_LABELS, builder.toString(), userId);
        }

        private static void ensureValidField(String value) {
            if (TextUtils.isEmpty(value)) {
                throw new IllegalArgumentException(value + " should not empty ");
            }
        }
    }
}
