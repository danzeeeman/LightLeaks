#include "ofApp.h"

using namespace ofxCv;
using namespace cv;

void ofApp::setup() {
	ofSetVerticalSync(true);
	ofSetFrameRate(120);
	
	//int proWidth = 1024, proHeight = 768;
	//int proWidth = 3 * 512, proHeight = 256;
	int proWidth = 1024*3, proHeight = 768;
	
	ofLoadImage(binaryCodedPix, "binaryCoded.png");
	ofLoadImage(camConfidencePix, "camConfidence.exr");
	ofLoadImage(minImagePix, "minImage.png");
	ofLoadImage(maxImagePix, "maxImage.exr");
	Mat binaryCoded = toCv(binaryCodedPix);
	Mat camConfidence = toCv(camConfidencePix);
	
	int camWidth = camConfidence.cols, camHeight = camConfidence.rows;
	vector< vector< vector<Vec3w> > > mapping(proHeight, vector< vector<Vec3w> >(proWidth));
	
	ofLogVerbose() << "building proMap";
	proMap = Mat::zeros(proHeight, proWidth, CV_16UC3);
	proConfidence = Mat::zeros(proHeight, proWidth, CV_32FC1);
	for(int cy = 0; cy < camHeight; cy++) {
		for(int cx = 0; cx < camWidth; cx++) {
			float curCamConfidence = camConfidence.at<float>(cy, cx);
			Vec3w pxy = binaryCoded.at<Vec3w>(cy, cx);
			unsigned short px = pxy[0], py = pxy[1];
			if(px < proWidth && py < proHeight) {
				Vec3w curProMap(cx, cy, 0);
				mapping[py][px].push_back(curProMap);
				float curProConfidence = proConfidence.at<float>(py, px);
				if(curCamConfidence > curProConfidence) {
					proConfidence.at<float>(py, px) = curCamConfidence;
					proMap.at<Vec3w>(py, px) = curProMap;
				}
			}
		}
	}
	
	cout << "processing mean and stddev" << endl;
	mean = Mat::zeros(proHeight, proWidth, CV_16UC3);
	stddev = Mat::zeros(proHeight, proWidth, CV_32FC3);
	count = Mat::zeros(proHeight, proWidth, CV_8UC1);
	for(int py = 0; py < proHeight; py++) {
		for(int px = 0; px < proWidth; px++) {
			cv::Scalar curMean, curStddev;
			meanStdDev(mapping[py][px], curMean, curStddev);
			mean.at<Vec3w>(py, px) = Vec3w(curMean[0], curMean[1], 0);
			stddev.at<Vec3f>(py, px) = Vec3f(curStddev[0] / proWidth, curStddev[1] / proWidth, 0);
			count.at<unsigned char>(py, px) += mapping[py][px].size();
		}
	}
	
	saveImage(proConfidence, "proConfidence.exr");
	saveImage(proMap, "proMap.png");
	saveImage(mean, "mean.png");
	saveImage(stddev, "stddev.exr");
	saveImage(count, "count.png");
}

void ofApp::update() {
}

void ofApp::draw() {
}