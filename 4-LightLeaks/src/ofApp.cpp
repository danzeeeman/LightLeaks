#include "ofApp.h"

const float trackScale = .25; // actual resizing on the image data
const float previewScale = .25; // presentation on the screen for calibration
const ofVec2f previewOffset(440, 248); // placement of debug image for calibration
const float backgroundLearningTime = 4; // in frames
const int foregroundBlur = 15; // after scaling by trackScale
const int foregroundDilate = 5; // connect disconnected parts. will fail to separate a big group of people.
const float contourFinderThreshold = 32; // higher blur calls for lower threshold
const float minAreaRadius = 5; // after scaling by trackScale
const float maxAreaRadius = 80; // after scaling by trackScale
const float lighthouseSpeed = 3;

const float durationIntermezzo = 30;
const float intervalIntermezzo = 30;
const int intermezzoCount = 5;
const float delaySpotlight = 1; // in and out delay
const int photoFrequency = 1; // every 1 spotlights

float cubicEaseInOut(float time, float duration=1.0, float startValue = 0.0, float valueChange = 1.0){
    float t = time;
    float d = duration;
    float b = startValue;
    float c = valueChange;
    
    t /= d/2.;
    if (t < 1) return c/2.*t*t*t + b;
    t -= 2.;
    return c/2.*(t*t*t + 2.) + b;
}

string getStageName(int stage) {
    switch(stage) {
        case 0: return "Lighthouse";
        case 1: return "Spotlight";
        case 2: return "Intermezzo";
        case 3: return "Linescan";
        default: return "Unknown";
    }
}


void ofApp::setup() {
    if(!setupCalled){
        oscSender.setup("localhost", 7777);

        setupCalled = true;
        ofSetLogLevel(OF_LOG_VERBOSE);
        ofSetVerticalSync(false);
        ofSetFrameRate(120);
        ofEnableAlphaBlending();
        
        //ofSetWindowPosition(1680,0);
        //ofSetWindowShape(1920*3, 1200);
        
        //Settings
        settings.load("settings.xml");
        config.load("config.xml");
        debugMode = config.getBoolValue("debugMode");
        ofSetWindowPosition(config.getIntValue("window/x"), config.getIntValue("window/y"));
        if(config.getBoolValue("fullscreen")) {
//            ofSetFullscreen(true);
        }
        
        previousTime = 0;
        
        //Shader
        shader.load("shader");
        
        xyzMap.load("../../../SharedData/xyzMap.exr");
        normalMap.load("../../../SharedData/normalMap.exr");
        confidenceMap.load("../../../SharedData/confidenceMap.exr");
        
        xyzMap.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
        normalMap.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
        confidenceMap.getTexture().setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
        
        stage = Lighthouse;
        substage = 0;
        
        //Spotlight setup
        spotlightPosition.setFc(0.01); //Low pass biquad filter - allow only slow frequencies
        
        kl.load("kl.jpg");
        
        setupSpeakers();
#ifdef USE_CAMERA
        photoCounter = 0;
        setupTracker();
#endif
        
    }
    
    int numWindows = windows.size();
    int curWindow = -1;
    for(int i=0;i<numWindows;i++){
        if(ofGetWindowPtr() == windows[i].get()){
            curWindow = i;
        }
    }
    
    if(curWindow == 0){
        
     //   dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            ofSetWindowPosition(0, 0);
            ofSetWindowShape(1920, 1200);
            
     
     //   });
    }
    if(curWindow == 1){
//        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            ofSetWindowPosition(1920, 0);
            ofSetWindowShape(1920*2,  1200);
            
      //  });
    }

    
}

void ofApp::setupSpeakers() {
    //Create the speaker fbo
    ofVec3f speakers[4];
    speakers[0] = ofVec3f(.2,1,0.43);
    speakers[1] = ofVec3f(.2,0,0.43);
    speakers[2] = ofVec3f(.2,0,0);
    speakers[3] = ofVec3f(.2,1,0);
    
    speakerXYZMap.allocate(4, 100, OF_IMAGE_COLOR_ALPHA);
    
    float speakerAreaSize = 0.02;
    
    float* pixels = speakerXYZMap.getPixels().getData();
    for(int y=0;y<speakerXYZMap.getHeight();y++){
        for(int x=0;x<speakerXYZMap.getWidth();x++){
            pixels[0] = speakers[x].x;
            pixels[1] = speakers[x].y + sin(y * TWO_PI / 20) * ((float)y/speakerXYZMap.getHeight()) * speakerAreaSize;
            pixels[2] = speakers[x].z + cos(y * TWO_PI / 20) * ((float)y/speakerXYZMap.getHeight()) * speakerAreaSize;
            pixels[3] = 1.0;
            pixels += 4;
        }
    }
    speakerXYZMap.update();
    
    speakerFbo.allocate(speakerXYZMap.getWidth(), speakerXYZMap.getHeight());
    speakerPixels.allocate(speakerXYZMap.getWidth(), speakerXYZMap.getHeight(),4);
}

void ofApp::update() {
    
    if(ofGetFrameNum() % 60 == 0){
        shader.load("shader");
    }
    
    int numWindows = windows.size();
    int curWindow = -1;
    for(int i=0;i<numWindows;i++){
        if(ofGetWindowPtr() == windows[i].get()){
            curWindow = i;
        }
    }
    
    if(curWindow == 0){
        
        //   dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        ofSetWindowPosition(0, 0);
        ofSetWindowShape(1920, 1200);
        
        
        //   });
    }
    if(curWindow == 1){
        //        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        ofSetWindowPosition(0+1920, 0);
        ofSetWindowShape(1920,  1200);
        
        //  });
    }

    
    float currentTime = ofGetElapsedTimef();
    dt = currentTime - previousTime;
    dt = ofClamp(dt, 0, .1);
    previousTime = currentTime;
    
    stageAge += dt;
    
    if(debugMode){
        if(debugStage == 0){
            stageGoal = Linescan;
            substage = scanDir;
        } else if(debugStage == 1){
            stageGoal = Lighthouse;
            substage = 0;
            lighthouseAngle = TWO_PI*mouseX/1920.0;
        }
        else if(debugStage == 2){
            stageGoal = Linescan;
            substage = 4;
        }
    } else {
        
        // if we're not on intermezzo
        if(stage != Intermezzo){
            // and enough time has passed since last stage change
            if(stageAge > intervalIntermezzo){
                // go to intermezzo
                stageGoal = Intermezzo;
            }
        }
        // if we're on intermezzo
        else {
            // but it's run its duration
            if(stageAge > durationIntermezzo){
                // and we're due for a spotlight
                if(spotlightThresholder == delaySpotlight){
                    // go to spotlight
                    stageGoal = Spotlight;
                }
                // if we're not due for a spotlight, return to lighthouse
                else {
                    // go to lighthouse
                    stageGoal = Lighthouse;
                }
            }
        }
        
        // if we're not already in spotlight
#ifdef USE_VIDEO
        if(stage != Spotlight){
            // and there are contours for enough time
            if(spotlightThresholder == delaySpotlight){
                // and we're not in intermezzo
                if(stage != Intermezzo) {
                    // go to spotlight
                    stageGoal = Spotlight;
                }
            }
        }
        // if we're on spotlight
        else {
            // and there haven't been contours for enough time
            if(spotlightThresholder == 0) {
                // go back to lighthouse
                stageGoal = Lighthouse;
            }
        }
#endif
        
        if(stage == Lighthouse){
            lighthouseAngle += dt * cubicEaseInOut(stageAmp) * lighthouseSpeed;
        }
        
     //   stageGoal = Intermezzo;
        
    }
    
    if(stage != stageGoal){
        stageAmp -= dt*0.5;
        if(stageAmp <= 0){
            stageAmp = 0;
            stageAge = 0;
            stage = stageGoal;
            startStage(stage);
#ifdef USE_CAMERA
            if(stage == Spotlight) {
                photoCounter = (photoCounter + 1) % photoFrequency;
                if(photoCounter == 0) {
                    ofDirectory::createDirectory("photos", true, true)	;
                    ofSaveImage(grabber.getColorPixels(), "photos/" + ofGetTimestampString() + ".jpg", OF_IMAGE_QUALITY_MEDIUM);
                }
            }
#endif
            if(stage == Intermezzo) {
                substage = (substage + 1) % intermezzoCount;
            }
        }
    } else {
        stageAmp = ofClamp(stageAmp+dt*0.5, 0, 1.);
    }
#ifdef USE_CAMERA
    updateTracker();
#endif
    updateOsc();
}

void ofApp::updateOsc() {
    if(stage == Lighthouse) {
        ofxOscMessage msg;
        msg.setAddress("/audio/lighthouse_angle");
        msg.addFloatArg(fmodf(lighthouseAngle/TWO_PI, 1));
        oscSender.sendMessage(msg);
    }
    
    if(stage == Spotlight) {
        ofxOscMessage msg;
        msg.setAddress("/audio/spotlight_position");
        float x = spotlightPosition.value().x;
        float y = spotlightPosition.value().y;
        x = ofMap(x, 0, 1, -1, 1);
        y = ofMap(y, .5, 0, -1, 1);
        msg.addFloatArg(y); // between Rs and R
        msg.addFloatArg(x); // between Ls and L
        oscSender.sendMessage(msg);
    }
}



void ofApp::draw() {
    int numWindows = windows.size();
    int curWindow = 0;
    for(int i=0;i<numWindows;i++){
        if(ofGetWindowPtr() == windows[i].get()){
            curWindow = i;
        }
    }
    
    ofBackground(0);
    ofEnableAlphaBlending();
    ofSetColor(255);
    
    //Lighthouse parameters
    float beamWidth = 0;
    if(stage == Lighthouse){
        // lighthouse is small most of the time, comes and goes from 0
        beamWidth = ofMap(cubicEaseInOut(stageAmp), 0, 1, 0, .3);
    }
    float spotlightSize = 0.2 * cubicEaseInOut(stageAmp);
    
    shader.begin(); {
        shader.setUniform1f("elapsedTime", ofGetElapsedTimef());
        shader.setUniform1f("beamAngle", fmodf(lighthouseAngle, TWO_PI));
        shader.setUniform1f("beamWidth", beamWidth);
        // convert where x is the long side of the room, y is short
        // into the model's coordinates where z is the long side, y is short
        shader.setUniform3f("spotlightPos",
                            0,
                            spotlightPosition.value().y,
                            spotlightPosition.value().x);
        shader.setUniform1f("spotlightSize", spotlightSize);
        shader.setUniform1i("stage", stage);
        shader.setUniform1i("substage", substage);
        shader.setUniform1f("stageAmp", stageAmp);
        shader.setUniform2f("mouse", ofVec2f((float)mouseX/1920, (float)mouseY/ofGetHeight()));
        shader.setUniformTexture("xyzMap", xyzMap, 0);
        //shader.setUniformTexture("normalMap", normalMap, 2);
        shader.setUniformTexture("confidenceMap", confidenceMap, 3);
        shader.setUniformTexture("kl", kl, 4);
        shader.setUniform1i("useConfidence", 1);
        
        // DRaw all projectors in one window
        //xyzMap.draw(0, 0, ofGetWidth(), ofGetHeight());
        
        // Draw 1 projector per window
        //xyzMap.drawSubsection(0, 0, ofGetWidth(), ofGetHeight(),curWindow * xyzMap.getWidth()/numWindows,0,xyzMap.getWidth()/numWindows, xyzMap.getHeight());
        
        // Special: Draw 1 projector in window 1, 2 projectors in window 2
        xyzMap.drawSubsection(0, 0, ofGetWidth(), ofGetHeight(),curWindow * xyzMap.getWidth()/2,0,ofGetWidth(), xyzMap.getHeight());
    } shader.end();
    
    
    //Debug text
    if(debugMode){
        ofSetColor(255);
        ofDrawBitmapStringHighlight(ofToString(ofGetFrameRate(), 0), 5, ofGetHeight() - 5);
        
        ofPushStyle();
        ofPushMatrix();
        ofTranslate(10, 120);
        ofDrawRectangle(0, 0, 4, stageAmp * 100);
        ofTranslate(14, 8);
        ofDrawBitmapStringHighlight(getStageName(stageGoal), 0, 0);
        ofDrawBitmapStringHighlight(getStageName(stage), 0, 100);
        ofPopMatrix();
        ofPopStyle();
    }
    
    //Speaker sampling code
    speakerFbo.begin(); {
        shader.begin();
        shader.setUniformTexture("xyzMap", speakerXYZMap, 0);
        shader.setUniform1i("useConfidence", 0);
        speakerXYZMap.draw(0,0);
        shader.end();
    } speakerFbo.end();
    
    
    //Read back the fbo, and average it on the CPU
    speakerFbo.getTexture().readToPixels(speakerPixels);
    
    ofxOscMessage brightnessMsg;
    brightnessMsg.setAddress("/audio/brightness");
    for(int s=0;s<4;s++){
        speakerAmp[s] = 0;
        for(int y=0;y<speakerFbo.getHeight();y++){
            speakerAmp[s] += speakerPixels.getColor(s, y)[0];
        }
        speakerAmp[s] /= speakerFbo.getHeight();
        brightnessMsg.addFloatArg(speakerAmp[s]);
    }
    oscSender.sendMessage(brightnessMsg);
    
    //Debug drawing
    if(debugMode){
        ofSetColor(255);
        speakerXYZMap.draw(0,0);
        speakerFbo.draw(10,0);
        
        ofPushMatrix();
        ofTranslate(20, 0);
        for(int i = 0; i < 4; i++) {
            ofDrawRectangle(0, 0, 4, 100 * speakerAmp[i]);
            ofTranslate(4, 0);
        }
        ofPopMatrix();
    }
    
    //Tracker
    /*if(debugMode){
     ofPushMatrix();
     ofTranslate(previewOffset);
     ofScale(previewScale, previewScale);
     
     ofPushMatrix();
     ofScale(1 / trackScale, 1 / trackScale);
     
     ofxCv::drawMat(grabberSmall, 0, 0);
     
     ofPushStyle();
     ofEnableBlendMode(OF_BLENDMODE_ADD);
     ofSetColor(ofColor::red);
     ofxCv::drawMat(grabberThresholded, 0, 0);
     ofPopStyle();
     
     contourFinder.draw();
     ofPopMatrix();
     
     ofPushStyle();
     ofSetColor(ofColor::yellow);
     ofNoFill();
     ofBeginShape();
     for(int i=0;i<4;i++){
     ofVertex(cameraCalibrationCorners[i].x, cameraCalibrationCorners[i].y);
     }
     ofVertex(cameraCalibrationCorners[0].x, cameraCalibrationCorners[0].y);
     ofEndShape(true);
     ofPopStyle();
     ofPopMatrix();
     
     ofDrawBitmapString("Spotlight pos "+ofToString(spotlightPosition.value().x,1)+" "+ofToString(spotlightPosition.value().y,1), 20, ofGetHeight() - 20);
     }*/
    
}

void ofApp::startStage(Stage stage) {
    ofxOscMessage msg;
    msg.setAddress("/audio/scene_change_event");
    msg.addIntArg(stage);
    oscSender.sendMessage(msg);
}

void ofApp::exit() {
#ifdef USE_CAMERA
    grabber.close();
#endif
}

void ofApp::keyPressed(int key) {
    if(key == 'f'){
        ofToggleFullscreen();
    }
    if(key == 'd'){
        debugMode = !debugMode;
    }
    if(key == 's'){
        shader.load("shader");
    }
    if(debugMode){
#ifdef USE_CAMERA
        if(key == '1'){
            setCorner = 0;
        }
        if(key == '2'){
            setCorner = 1;
        }
        if(key == '3'){
            setCorner = 2;
        }
        if(key == '4'){
            setCorner = 3;
        }
        if(key == OF_KEY_BACKSPACE){
            if(setCorner != -1){
                cameraCalibrationCorners[setCorner] = ofVec2f(settings.getValue("corner"+ofToString(setCorner)+"x",0),
                                                              settings.getValue("corner"+ofToString(setCorner)+"y",0));
                
                updateCameraCalibration();
            }
            setCorner = -1;
        }
        if(key == '\t') {
            float x = ofMap(mouseX, 0, 1920, 0, 1);
            float y = ofMap(mouseY, 0, ofGetHeight(), 0, .5);
            spotlightPosition.update(ofVec2f(x, y));
            spotlightThresholder = delaySpotlight;
        }

#endif
        if(key == 'x'){
            debugStage = 0;
            scanDir = 0;
        }
        if(key == 'y'){
            debugStage = 0;
            scanDir = 1;
        }
        if(key == 'z'){
            debugStage = 0;
            scanDir = 2;
        }
        if(key == 'l'){
            debugStage = 1;
        }
        if(key == 'c'){
            debugStage = 2;
        }
    }
}

void ofApp::mouseMoved(int x, int y){
    /*  if(setCorner != -1 ){
     cameraCalibrationCorners[setCorner] = (ofVec2f(x, y) - previewOffset) / previewScale;
     updateCameraCalibration();
     
     }*/
}

void ofApp::mousePressed( int x, int y, int button ){
    /* if(setCorner != -1){
     settings.setValue("corner"+ofToString(setCorner)+"x", int(cameraCalibrationCorners[setCorner].x));
     settings.setValue("corner"+ofToString(setCorner)+"y", int(cameraCalibrationCorners[setCorner].y));
     settings.save("settings.xml");
     
     setCorner = -1;
     }*/
}


#ifdef USE_CAMERA
void ofApp::updateCameraCalibration(){
    ofVec2f inputCorners[4];
    // these x,y correspond to the z,y of the 3d model
    inputCorners[0] = ofVec2f(0,.5);
    inputCorners[1] = ofVec2f(1,.5);
    inputCorners[2] = ofVec2f(1,0);
    inputCorners[3] = ofVec2f(0,0);
    
    cameraCalibration.calculateMatrix(inputCorners, cameraCalibrationCorners);
}

void ofApp::setupTracker() {
    //Tracker
    grabber.setup(1920, 1080, 25);
    
    contourFinder.setSortBySize(true); // makes sorting consistent
    contourFinder.setMinAreaRadius(minAreaRadius);
    contourFinder.setMaxAreaRadius(maxAreaRadius);
    
    cameraCalibrationCorners[0] = ofVec2f(settings.getValue("corner0x",0),
                                          settings.getValue("corner0y",0));
    cameraCalibrationCorners[1] = ofVec2f(settings.getValue("corner1x",1920),
                                          settings.getValue("corner1y",0));
    cameraCalibrationCorners[2] = ofVec2f(settings.getValue("corner2x",1920),
                                          settings.getValue("corner2y",1080));
    cameraCalibrationCorners[3] = ofVec2f(settings.getValue("corner3x",0),
                                          settings.getValue("corner3y",1080));
    
    setCorner = -1;
    firstFrame = true;
    
    updateCameraCalibration();
}

void ofApp::updateTracker() {
    bool newFrame = grabber.update();
    
    if(newFrame ) {
        ofPixels& pixels = grabber.getGrayPixels();
        if(pixels.getWidth()>0){
            ofxCv::resize(pixels, grabberSmall, trackScale, trackScale, cv::INTER_AREA);
            //            cameraBackground.setDifferenceMode(ofxCv::RunningBackground::BRIGHTER); // works on floor, but not walls or if couches are white
            cameraBackground.setLearningTime(backgroundLearningTime);
            cameraBackground.update(grabberSmall, grabberThresholded);
            if(firstFrame){ // first frame from ofxBlackMagic is useless
                cameraBackground.reset();
            }
            ofxCv::blur(grabberThresholded, foregroundBlur);
            ofxCv::dilate(grabberThresholded, foregroundDilate);
            contourFinder.setThreshold(contourFinderThreshold);
            contourFinder.findContours(grabberThresholded);
            
            if(contourFinder.getContours().size() > 0){
                logAudience();
                ofDrawRectangleangle rect = ofxCv::toOf(contourFinder.getBoundingRect(0));
                ofVec2f point = rect.getTopLeft(); // corresponds to feet due to angle of camera
                point /= trackScale;
                spotlightPosition.update(cameraCalibration.inversetransform(point));
            }
            
            firstFrame = false;
        }
    }
    
    if(contourFinder.getContours().size() > 0){
        spotlightThresholder += dt;
    } else {
        spotlightThresholder -= dt;
    }
    spotlightThresholder = ofClamp(spotlightThresholder, 0, delaySpotlight);
}

void ofApp::logAudience() {
    string logLine = buildContourLogLine(contourFinder);
    ofFile out("log.txt", ofFile::Append);
    out << logLine << endl;
}

#include "Poco/DateTimeFormat.h"
string buildContourLogLine(ofxCv::ContourFinder& finder) {
    int n = finder.size();
    vector<string> record;
    record.push_back(ofGetTimestampString(Poco::DateTimeFormat::ISO8601_FRAC_FORMAT));
    record.push_back(ofToString(n));
    for(int i = 0; i < n; i++) {
        stringstream str;
        cv::Rect rect = finder.getBoundingRect(i);
        str << rect.x << " " << rect.y << " " << rect.width << " " << rect.height;
        record.push_back(str.str());
    }
    return ofJoinString(record, "\t");
}
#endif


