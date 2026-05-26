package com.mehmetunal.audienceharmonicsynth;

import android.os.Bundle;
import android.view.WindowManager;

public final class AudienceHarmonicSynthActivity extends android.app.Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        super.onCreate(savedInstanceState);
    }
}
