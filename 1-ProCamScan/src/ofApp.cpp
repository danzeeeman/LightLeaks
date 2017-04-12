#include "ofApp.h"

#include "LightLeaksUtilities.h"

using namespace ofxCv;
using namespace cv;

#define USE_GDC
#define SAVE_DEBUG
//#define USE_LCP

// Situations like capturing a lcd screen, highpass should be disabled since it blurs the image
#define RUN_HIGHPASS

// 180 is good with reflections that are about 30x30px
// (similar to photoshop's "30px")
// this should scale to match the size of the reflections in the image
int highpassBlurSize = 300;

int calibrationMode = INTER_CUBIC;

bool natural(const ofFile& a, const ofFile& b) {
	string aname = a.getBaseName(), bname = b.getBaseName();
	int aint = ofToInt(aname), bint = ofToInt(bname);
	if(ofToString(aint) == aname && ofToString(bint) == bname) {
		return aint < bint;
	} else {
		return a < b;
	}
}

void processGraycodeLevel(int i, int n, int dimensions, const Mat& cameraMask, Mat& confidence, Mat& binaryCoded, Mat& minMat, Mat& maxMat, ofImage * imageNormal, ofImage * imageInverse) {
    ofLogVerbose() << "Process " << i << " of " << n;
    
	int w = imageNormal->getWidth(), h = imageNormal->getHeight();
	cv::Mat imageNormalGray, imageInverseGray;
    imageNormalGray = toCv(*imageNormal);
    imageInverseGray = toCv(*imageInverse);
    
    if(cameraMask.cols > 0){
        imageNormalGray &= cameraMask;
        imageInverseGray &= cameraMask;
    }
    
	if(i == 0) {
		minMat = min(imageNormalGray, imageInverseGray);
		maxMat = max(imageNormalGray, imageInverseGray);
	} else {
		min(minMat, imageNormalGray, minMat);
		min(minMat, imageInverseGray, minMat);
		max(maxMat, imageNormalGray, maxMat);
		max(maxMat, imageInverseGray, maxMat);
	}
    
    unsigned short curMask = 1 << (n - i - 1);
	for(int y = 0; y < h; y++) {
		for(int x = 0; x < w; x++) {
			const unsigned char& normal = imageNormalGray.at<unsigned char>(y, x);
			const unsigned char& inverse = imageInverseGray.at<unsigned char>(y, x);
			if(normal > inverse) {
				binaryCoded.at<unsigned short>(y, x) |= curMask;
			}
			float range = fabsf((float) normal - (float) inverse);
            confidence.at<float>(y, x) += range;
		}
    }
    
//    saveImage(imageNormalGray, "normal-gray-" + ofToString(i) + ".jpg");
//    saveImage(imageInverseGray, "inverse-gray-" + ofToString(i) + ".jpg");
//    saveImage(confidence, "confidence-" + ofToString(i) + ".exr");
//    cout << "cur variation: " << curVariation << endl;
}

// allocates two 32f Mats each time
void highpass(Mat img) {
#ifdef RUN_HIGHPASS
    Mat img32f, blur32f;
    ofxCv::copy(img, img32f, CV_32FC1);
    ofxCv::GaussianBlur(img32f, blur32f, highpassBlurSize);
    img32f -= blur32f;
    img32f += .5; // center on gray before conversion to 8 bit
    ofxCv::copy(img32f, img, CV_8UC1);
#endif
}

void ofApp::setup() {
	ofSetVerticalSync(true);
	ofSetFrameRate(120);
    setCalibrationDataPathRoot();
    ofSetLogLevel(OF_LOG_VERBOSE);
    
#ifdef USE_LCP
    string lcpRoot = "/Library/Application Support/Adobe/CameraRaw/LensProfiles/1.0/Canon/";
    calibration.loadLcp(lcpRoot + "Canon EOS 50D (Canon EF-S 18-135mm f3.5-5.6 IS) - RAW.lcp", 18);
#endif
    
    // projector mask
    Mat projectorMaskMat;
    ofFile projectorMaskFile("mask.png");
    if(projectorMaskFile.exists()){
        ofLogVerbose() << "Projector mask loaded";
        projectorMask.load(projectorMaskFile);
        projectorMask.setImageType(OF_IMAGE_GRAYSCALE);
        copy(projectorMask, projectorMaskMat, CV_32FC1);
        ofLogVerbose() << "Built mask with " << projectorMaskMat.cols << "x" << projectorMaskMat.rows;
    } else {
        ofLogVerbose() << "No file called mask.png in SharedData/ folder. Continuing without a projector mask";
    }

    vector<ofFile> scans = getScanNames();
    for(int scan=0;scan<scans.size();scan++){
		if(scans[scan].isFile()) {
			continue;
		}
		
        string scanName = scans[scan].getFileName();
        string path = scans[scan].path()+"/";
		
        string camMaskPath = path+"/mask.png";
        ofFile proConfidenceFile = ofFile(path+"/proConfidence.exr");
        ofFile proMapFile = ofFile(path+"/proMap.png");

        bool outputFilesExist = proConfidenceFile.exists() || proMapFile.exists();
        if(outputFilesExist){
            ofLogVerbose()<<"Skipping "<<scanName<<" since output files (SharedData/"<<scanName<<"/proMaps) already exist";
        }
        if(scanName[0] == '_'){
            ofLogVerbose()<<"Skipping "<<scanName<<" since it's underscored";
        }
        
        //Skip folders with underscore
        if(!outputFilesExist && scanName[0] != '_'){
            
            ofLogVerbose()<<"ProCamScan "<<scanName<<endl;
            
            ofSetLogLevel(OF_LOG_ERROR);
            
            dirHorizontalNormal.listDir(path + "cameraImages/horizontal/normal/");
            dirHorizontalInverse.listDir(path + "cameraImages/horizontal/inverse/");
            dirVerticalNormal.listDir(path + "cameraImages/vertical/normal/");
            dirVerticalInverse.listDir(path + "cameraImages/vertical/inverse/");
            hnFiles = dirHorizontalNormal.getFiles();
            hiFiles = dirHorizontalInverse.getFiles();
            vnFiles = dirVerticalNormal.getFiles();
            viFiles = dirVerticalInverse.getFiles();
            
            ofSetLogLevel(OF_LOG_VERBOSE);
            
            ofSort(hnFiles, natural);
            ofSort(hiFiles, natural);
            ofSort(vnFiles, natural);
            ofSort(viFiles, natural);
            horizontalBits = dirHorizontalNormal.size();
            verticalBits = dirVerticalNormal.size();
            
            //Error handling
            if(horizontalBits == 0){
                ofLogError() << "No horizontal images found (searching in SharedData/"+path+"cameraImages/horizontal/normal). Quitting";
                ofExit();
            }
            if(verticalBits == 0){
                ofLogError() << "No vertical images found (searching in SharedData/"+path+"cameraImages/vertical/normal). Quitting";
                ofExit();
            }
            if(dirHorizontalInverse.size() != dirHorizontalNormal.size()){
                ofLogError() << "Mismatch in number of horizontal images ("+ofToString(dirHorizontalNormal.size())+" normal images and "+ofToString(dirHorizontalInverse.size())+" inverse images)";
                ofExit();
            }
            if(dirVerticalInverse.size() != dirVerticalNormal.size()){
                ofLogError() << "Mismatch in number of vertical images ("+ofToString(dirVerticalNormal.size())+" normal images and "+ofToString(dirVerticalInverse.size())+" inverse images)";
                ofExit();
            }
            
            //Camera mask
            bool maskLoaded = cameraMask.load(camMaskPath);
            cameraMask.setImageType(OF_IMAGE_GRAYSCALE);
            if(!maskLoaded){
                ofLogVerbose() << "No file called mask.png in SharedData/"+scanName+" folder. Continuing without a camera mask";
            } else {
                ofLogVerbose() << "Camera mask loaded";
            }
            
            ofImage prototype;
            prototype.load(path + "cameraImages/horizontal/normal/0.jpg");
            camWidth = prototype.getWidth(), camHeight = prototype.getHeight();
            
            camConfidence = Mat::zeros(camHeight, camWidth, CV_32FC1);
            binaryCodedHorizontal = Mat::zeros(camHeight, camWidth, CV_16UC1);
            binaryCodedVertical = Mat::zeros(camHeight, camWidth, CV_16UC1);
            
            Mat cameraMaskMat;
            if(maskLoaded){
                cameraMaskMat = toCv(cameraMask);
#ifdef USE_LCP
                calibration.undistort(cameraMaskMat, calibrationMode);
#endif
            }
            
            cout << "converted to mat " << cameraMaskMat.rows << "x" << cameraMaskMat.cols << endl;
            
            hnImageNormal.resize(horizontalBits, 0);
            hnImageInverse.resize(horizontalBits, 0);
            
            viImageNormal.resize(verticalBits, 0);
            viImageInverse.resize(verticalBits, 0);
            
#ifdef USE_GDC
            //A dispatch group that the horizontal job and vertical job will be added to
            dispatch_group_t group = dispatch_group_create();

            //Create the async job for handling horizontal images
            dispatch_group_async(group, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH,0), ^{
#endif
                //Load Horizontal images
                ofLogVerbose() << "Loading " <<  horizontalBits << " horizontal images";
#ifdef USE_GDC
                dispatch_apply(horizontalBits, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t i){
#else
               for(int i = 0; i < horizontalBits; i++) {
#endif
                    ofImage * img = new ofImage();
                    img->setUseTexture(false);
                    
                    ofImage * imgI = new ofImage();
                    imgI->setUseTexture(false);
                    
                    // ofLogVerbose() <<"Load "+hnFiles[i].path()<<endl;
                   img->load(hnFiles[i].path());
                   imgI->load(hiFiles[i].path());
                   
                   img->setImageType(OF_IMAGE_GRAYSCALE);
                   imgI->setImageType(OF_IMAGE_GRAYSCALE);
                   
                   Mat bufferMat;
                   
                   Mat distortedMat = toCv(*img);
                   highpass(distortedMat);
#ifdef USE_LCP
                   copy(distortedMat, bufferMat);
                   calibration.undistort(bufferMat, distortedMat, calibrationMode);
#endif
                   
                   Mat distortedMatI = toCv(*imgI);
                   highpass(distortedMatI);
#ifdef USE_LCP
                   copy(distortedMatI, bufferMat);
                   calibration.undistort(bufferMat, distortedMatI, calibrationMode);
#endif
                   
                   hnImageNormal[i] = img;
                   hnImageInverse[i] = imgI;
                }
#ifdef USE_GDC
                );
#endif
                
                
                //Process horizontal images
                ofLogVerbose() << "Process " <<  horizontalBits << " horizontal images";
                for(int i = 0; i < horizontalBits; i++) {
                    processGraycodeLevel(i, horizontalBits, 2, cameraMaskMat, camConfidence, binaryCodedHorizontal, minImage, maxImage, hnImageNormal[i], hnImageInverse[i]);
                };
                
                
                //Delete horizontal images
                for(int i = 0; i < horizontalBits; i++) {
                    delete hnImageNormal[i];
                    delete hnImageInverse[i];
                }
#ifdef USE_GDC
            });
            
            //Create the async job for handling vertical images
            dispatch_group_async(group, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH,0), ^{
#endif
                
                //Load Vertical images
                ofLogVerbose() << "Loading " <<  verticalBits << " vertical images";

#ifdef USE_GDC
                dispatch_apply(verticalBits, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t i) {
#else
                for(int i = 0; i < verticalBits; i++) {
#endif
                    
                    ofImage * img = new ofImage();
                    img->setUseTexture(false);
                    
                    ofImage * imgI = new ofImage();
                    imgI->setUseTexture(false);
                    
                    img->load(vnFiles[i].path());
                    imgI->load(viFiles[i].path());
                    
                    img->setImageType(OF_IMAGE_GRAYSCALE);
                    imgI->setImageType(OF_IMAGE_GRAYSCALE);
                    
                    Mat bufferMat;
                    Mat distortedMat = toCv(*img);
                    highpass(distortedMat);
#ifdef USE_LCP
                    copy(distortedMat, bufferMat);
                    calibration.undistort(bufferMat, distortedMat, calibrationMode);
#endif

                    Mat distortedMatI = toCv(*imgI);
                    highpass(distortedMatI);
#ifdef USE_LCP
                    copy(distortedMatI, bufferMat);
                    calibration.undistort(bufferMat, distortedMatI, calibrationMode);
#endif
                    
                    viImageNormal[i] = img;
                    viImageInverse[i] = imgI;
                }
#ifdef USE_GDC
                );
#endif
                
                
                //Process vertical images
                ofLogVerbose() << "Process " <<  verticalBits << " vertical images";
                for(int i = 0; i < verticalBits; i++) {
                    processGraycodeLevel(i, verticalBits, 2, cameraMaskMat, camConfidence, binaryCodedVertical, minImage, maxImage, viImageNormal[i], viImageInverse[i]);
                }
                
                //Delete vertical images
                for(int i = 0; i < verticalBits; i++) {
                    delete viImageNormal[i];
                    delete viImageInverse[i];
                }
#ifdef USE_GDC
            });
            
            //Wait for the group to finish before continuing
            dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
#endif
            

            hnImageInverse.clear();
            hnImageNormal.clear();
            
            viImageInverse.clear();
            viImageNormal.clear();  

 
            // convert camConfidence to 0-1 range
            camConfidence /= 255 * (horizontalBits + verticalBits);
            
            grayToBinary(binaryCodedHorizontal, horizontalBits);
            grayToBinary(binaryCodedVertical, verticalBits);
            
#ifdef SAVE_DEBUG
            ofLogVerbose() << "saving debug results";
            saveImage(camConfidence, path+"/camConfidence.exr");
            saveImage(minImage, path+"/minImage.jpg", OF_IMAGE_QUALITY_LOW);
            saveImage(maxImage, path+"/maxImage.jpg", OF_IMAGE_QUALITY_LOW);
#endif
            ofLogVerbose() << "building and saving reference image";
            Mat referenceImage;
            ofxCv::equalizeHist(minImage, referenceImage);
            saveImage(referenceImage, path+"/referenceImage.jpg", OF_IMAGE_QUALITY_LOW);
            
            Mat binaryCoded, emptyChannel;
            emptyChannel = Mat::zeros(camHeight, camWidth, CV_16UC1);
            vector<Mat> channels;
            channels.push_back(binaryCodedVertical);
            channels.push_back(binaryCodedHorizontal);
            channels.push_back(emptyChannel);
            merge(channels, binaryCoded);

//            ofLogVerbose() << "saving binaryCoded";
//            saveImage(binaryCoded, path+"/binaryCoded.png");
            
            ofLogVerbose() << "Build Pro Map";
			
			ofXml settings("settings.xml");
            proWidth = settings.getIntValue("projectors/width");
			proHeight = settings.getIntValue("projectors/height");
			proCount = settings.getIntValue("projectors/count");
//            buildProMap(proCount * proWidth, proHeight,
//                        binaryCoded,
//                        camConfidence,
//                        proConfidence,
//                        proMap);
            buildProMapDist(proCount * proWidth, proHeight,
                        binaryCoded,
                        camConfidence,
                        proConfidence,
                        proMap,
                        3);
                
            if(!projectorMaskMat.empty()) {
                cv::multiply(projectorMaskMat, proConfidence, proConfidence);
            }

            saveImage(proConfidence, path+"/proConfidence.exr");
            saveImage(proMap, path+"/proMap.png");
        }
    }
    time = ofGetElapsedTimef();
    ofLogVerbose() <<" Done in "+ofToString(ofGetElapsedTimef())+" seconds";
}

void ofApp::update() {
}

void ofApp::draw() {
    ofBackground(30);
    ofSetColor(200);
    ofDrawBitmapString("I'm done....\nIt took me "+ofToString(time)+" seconds", ofPoint(10,20));
}
