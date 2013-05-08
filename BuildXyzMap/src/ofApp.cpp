#include "ofApp.h"

using namespace cv;
using namespace ofxCv;

void ofApp::setup() {
	ofSetLogLevel(OF_LOG_VERBOSE);
	ofSetVerticalSync(true);
	ofSetFrameRate(120);
	
	float threshold = .2;
	int scaleFactor = 4;
	
	ofDirectory dir;
	dir.listDir(".");
	for(int i = 0; i < dir.size(); i++) {
		if(dir.getFile(i).isDirectory()) {
			string path = dir.getPath(i);
			ofLogVerbose() << "processing " << path;
			ofFloatImage xyzMap, proConfidence;
			ofShortImage proMap;
			
			xyzMap.loadImage(path + "/xyzMap.exr");
			proConfidence.loadImage(path + "/proConfidence.exr");
			proMap.loadImage(path + "/proMap.png");
			Mat xyzMapMat = toCv(xyzMap);
			Mat proConfidenceMat = toCv(proConfidence);
			Mat proMapMat = toCv(proMap);
			
			if(proXyzCombined.cols == 0) {
				proXyzCombined = Mat::zeros(proMapMat.rows, proMapMat.cols, CV_32FC4);
				proConfidenceCombined = Mat::zeros(proConfidenceMat.rows, proConfidenceMat.cols, CV_32FC1);
			}
			
			int w = proXyzCombined.cols, h = proXyzCombined.rows;
			for(int y = 0; y < h; y++) {
				for(int x = 0; x < w; x++) {
					float cur = proConfidenceMat.at<float>(y, x);
					if(cur > proConfidenceCombined.at<float>(y, x) && cur > threshold) {
						proConfidenceCombined.at<float>(y, x) = proConfidenceMat.at<float>(y, x);
						Vec3w cur = proMapMat.at<Vec3w>(y, x);
						proXyzCombined.at<Vec4f>(y, x) = xyzMapMat.at<Vec4f>(cur[1] / scaleFactor, cur[0] / scaleFactor);
					}
				}
			}
		}
	}
	
	ofLogVerbose() << "saving results";
	ofFloatPixels proMapFinal, proConfidenceFinal;
	toOf(proXyzCombined, proMapFinal);
	toOf(proConfidenceCombined, proConfidenceFinal);
	ofSaveImage(proMapFinal, "proXyz.exr");
	ofSaveImage(proConfidenceFinal, "proConfidence.exr");
}

void ofApp::update() {
	
}

void ofApp::draw() {
	ofBackground(0);
}
