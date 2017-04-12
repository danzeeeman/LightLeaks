#include "ofMain.h"
#include "EdsdkOsc.h"
#include "GrayCodeGenerator.h"

bool primary;
int monitorWidth;
int totalPhysicalProjectors, totalProjectors, tw, th;

class testApp : public ofBaseApp {
public:
	ofImage mask;
	EdsdkOsc camera;
    
	GrayCodeGenerator generator;
	bool capturing = false;
	int totalDirection = 2;
	int totalInverse = 2;
	
	// every combination of these four properties
	int projector = 0; // n values = totalProjectors
	int direction = 0; // false/true, 2 values
	int inverse = 0; // false/true, 2 values
	int pattern = 0; // depends on projector resolution
	
	string curDirectory;
	long bufferTime = 100;
	bool needToCapture = false;
	long captureTime = 0;
	bool referenceImage = false;
    
    bool generated;
    string timestamp;
    
	void generate() {
		generator.setSize(tw, th);
		generator.setOrientation(direction == 0 ? PatternGenerator::VERTICAL : PatternGenerator::HORIZONTAL);
		generator.setInverse(inverse == 0);
		generator.generate();
		stringstream dirStr;
		dirStr <<
        "../../../SharedData/scan-"<< timestamp << "/cameraImages/" <<
		(direction == 0 ? "vertical/" : "horizontal/") <<
		(inverse == 0 ? "inverse/" : "normal/");
		curDirectory = dirStr.str();
        ofLog() << "Creating " << curDirectory;
		camera.createDirectory(curDirectory);
        generated = true;
	}
	
	bool nextState() {
		pattern++;
		if(pattern == generator.size()) {
			pattern = 0;
			inverse++;
			if(inverse == totalInverse) {
				inverse = 0;
				direction++;
				if(direction == totalDirection) {
					direction = 0;
					projector++;
					if(projector == totalProjectors) {
						projector = 0;
						return false;
					}
				}
			}
			generate();
		}
		return true;
	}
	
    void setup() {
		ofSetVerticalSync(true);
		ofHideCursor();
		if(ofFile::doesFileExist("../../../SharedData/mask.png")) {
			mask.load("../../../SharedData/mask.png");
            ofLog() << "Loaded mask";
            ofEnableBlendMode(OF_BLENDMODE_MULTIPLY);
            ofBackground(255);
        } else {
            ofEnableAlphaBlending();
            ofBackground(0);
        }
        
        // this will need to be modified if running from computer with monitor vs no monitor
        ofSetWindowPosition(monitorWidth, 0);
        ofSetWindowShape(tw*totalPhysicalProjectors, th);
		ofSetLogLevel(OF_LOG_VERBOSE);
        camera.setup();
	}
	
	void update() {
		if(!capturing) {
			camera.update();
            
            if(camera.start){
                camera.start = false;
                
                pattern = 0;
                projector = 0;
                direction = 0;
                inverse = 0;
                
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                    camera.takePhoto(curDirectory + ofToString(pattern) + ".jpg", primary);
                });
                
                timestamp = ofToString(ofGetHours())+ofToString(ofGetMinutes());
                generate();

                capturing = true;
            }
		}
		if(camera.isPhotoNew()) {
            if(camera.error){
                captureTime = ofGetElapsedTimeMillis();
                needToCapture = true;
            } else {
                if(nextState()) {
                    captureTime = ofGetElapsedTimeMillis();
                    needToCapture = true;
                } else {
                    cout<<"Done taking photos. Hurray!"<<endl;
                    capturing = false;
                }
            }
            
		}
	}
	
    void draw() {
        int singleWidth = tw / totalPhysicalProjectors;
        
        if(!primary) {
            float offset = (totalPhysicalProjectors / 2) * singleWidth;
            ofTranslate(-offset, 0);
        }
        
		ofSetColor(255);
       
		if(!capturing) {

			/*ofPushMatrix();
			ofScale(.25, .25);
			camera.draw(0, 0);
			ofPopMatrix();*/
            
            for(int i = 0; i < totalPhysicalProjectors; i++) {
                float hue = ofMap(i, 0, totalPhysicalProjectors, 0, 255);
                ofSetColor(ofColor::fromHsb(hue, 255, 255));
                ofDrawRectangle(i * singleWidth, 0, singleWidth, th);
            }
        } else {
            if(generated){
                generator.get(pattern).draw(projector * tw, 0);
            }
            if(mask.getWidth() > 0) {
                mask.draw(0, 0);
            }
        }
		if(needToCapture && (ofGetElapsedTimeMillis() - captureTime) > bufferTime) {
            camera.takePhoto(curDirectory + ofToString(pattern) + ".jpg", primary);
			needToCapture = false;
		}
	}
	
	void keyPressed(int key) {
		if(key == OF_KEY_RIGHT) {
			pattern++;
		}
        if(key == OF_KEY_LEFT) {
			pattern--;
		}
		if(key == ' ') {
            timestamp = ofToString(ofGetHours())+ofToString(ofGetMinutes());
            generate();
            
			camera.takePhoto(curDirectory + ofToString(pattern) + ".jpg", primary);
			capturing = true;
		}
		if(key == 'r') {
			camera.takePhoto(curDirectory + ofToString(pattern) + ".jpg", primary);
			referenceImage = true;
		}
		if(key == 'f') {
			ofToggleFullscreen();
		}
	}
};

int main() {
	ofXml settings;
	settings.load("../../../SharedData/settings.xml");
	totalProjectors = settings.getIntValue("projectors/count");
    totalPhysicalProjectors = totalProjectors;
	tw = settings.getIntValue("projectors/width");
	th = settings.getIntValue("projectors/height");
    
    monitorWidth = settings.getIntValue("monitorWidth");
    primary = settings.getBoolValue("primary");
    cout << "running as primary: " << primary << endl;
    
    if(settings.getBoolValue("sample/singlepass")) {
        ofLog() << "Using single pass sampling";
        tw *= totalProjectors;
        totalProjectors = 1;
    }
	
    ofGLFWWindowSettings win;
    
    win.width = 1024;
    win.height = 768;
    win.multiMonitorFullScreen = true;
    
    ofCreateWindow(win)->setFullscreen(true);
    ofRunApp(new testApp());
}
