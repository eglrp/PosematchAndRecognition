#include "StereoMatch.h"


StereoMatch::StereoMatch(void)
	: m_frameWidth(0), m_frameHeight(0), m_numberOfDisparies(0)
{
}

StereoMatch::~StereoMatch(void)
{
}


/*----------------------------
* 功能 : 初始化内部变量，载入双目定标结果数据
*----------------------------
* 函数 : StereoMatch::init
* 访问 : public
* 返回 : 0 - 载入定标数据失败，1 - 载入定标数据成功
*
* 参数 : imgWidth		[in]	图像宽度
* 参数 : imgHeight		[in]	图像高度
* 参数 : xmlFilePath	[in]	双目定标结果数据文件
*/
int StereoMatch::init(int imgWidth, int imgHeight, const char* xmlFilePath,string _remapXml)
{
	m_frameWidth = imgWidth;
	m_frameHeight = imgHeight;
	m_numberOfDisparies = 0;
	xmlFile = xmlFilePath;
	remapXml = _remapXml;
	return loadCalibData(xmlFilePath);
}


/*----------------------------
* 功能 : 载入双目定标结果数据
*----------------------------
* 函数 : StereoMatch::loadCalibData
* 访问 : public
* 返回 : 1		成功
*		 0		读入校正参数失败
*		 -1		定标参数的图像尺寸与当前配置的图像尺寸不一致
*		 -2		校正方法不是 BOUGUET 方法
*		 -99	未知错误
*
* 参数 : xmlFilePath	[in]	双目定标结果数据文件
*/
int StereoMatch::loadCalibData(const char* xmlFilePath)
{
	// 读入摄像头定标参数 Q roi1 roi2 mapx1 mapy1 mapx2 mapy2
	try
	{
		cv::FileStorage fs(xmlFilePath, cv::FileStorage::READ);
		if (!fs.isOpened())
		{
			return (0);
		}

		cv::Size imageSize;
		cv::FileNodeIterator it = fs["imageSize"].begin();
		it >> imageSize.width >> imageSize.height;
		if (imageSize.width != m_frameWidth || imageSize.height != m_frameHeight)
		{
			return (-1);
		}

		vector<int> roiVal1;
		vector<int> roiVal2;
		fs["leftCameraMatrix"] >> cameraMatrix_L;
		fs["leftDistortCoefficients"] >> distortionCoefficients_L;
		fs["rightCameraMatrix"] >> cameraMatrix_R;
		fs["rightDistortCoefficients"] >> distortionCoefficients_R;
		fs["foundationalMatrix"] >> FunMat;


		fs["leftValidArea"] >> roiVal1;
		m_Calib_Roi_L.x = roiVal1[0];
		m_Calib_Roi_L.y = roiVal1[1];
		m_Calib_Roi_L.width = roiVal1[2];
		m_Calib_Roi_L.height = roiVal1[3];

		fs["rightValidArea"] >> roiVal2;
		m_Calib_Roi_R.x = roiVal2[0];
		m_Calib_Roi_R.y = roiVal2[1];
		m_Calib_Roi_R.width = roiVal2[2];
		m_Calib_Roi_R.height = roiVal2[3];

		fs["QMatrix"] >> m_Calib_Mat_Q;
		fs["remapX1"] >> m_Calib_Mat_Remap_X_L;
		fs["remapY1"] >> m_Calib_Mat_Remap_Y_L;
		fs["remapX2"] >> m_Calib_Mat_Remap_X_R;
		fs["remapY2"] >> m_Calib_Mat_Remap_Y_R;

		cv::Mat lfCamMat;
		fs["leftCameraMatrix"] >> lfCamMat;
		m_FL = lfCamMat.at<double>(0, 0);

		m_Calib_Mat_Q.at<double>(3, 2) = -m_Calib_Mat_Q.at<double>(3, 2);

		m_Calib_Mat_Mask_Roi = cv::Mat::zeros(m_frameHeight, m_frameWidth, CV_8UC1);
		cv::rectangle(m_Calib_Mat_Mask_Roi, m_Calib_Roi_L, cv::Scalar(255), -1);

		m_BM.state->roi1 = m_Calib_Roi_L;
		m_BM.state->roi2 = m_Calib_Roi_R;

		m_Calib_Data_Loaded = true;

		string method;
		fs["rectifyMethod"] >> method;
		if (method != "BOUGUET")
		{
			return (-2);
		}

	}
	catch (std::exception& e)
	{
		m_Calib_Data_Loaded = false;
		return (-99);
	}

	return 1;
}


/*----------------------------
* 功能 : 基于 BM 算法计算视差
*----------------------------
* 函数 : StereoMatch::bmMatch
* 访问 : public
* 返回 : 0 - 失败，1 - 成功
*
* 参数 : frameLeft		[in]	左摄像机帧图
* 参数 : frameRight		[in]	右摄像机帧图
* 参数 : disparity		[out]	视差图
* 参数 : imageLeft		[out]	处理后的左视图，用于显示
* 参数 : imageRight		[out]	处理后的右视图，用于显示
*/
int StereoMatch::bmMatch(cv::Mat& frameLeft, cv::Mat& frameRight, cv::Mat& disparity, cv::Mat& imageLeft, cv::Mat& imageRight)
{
	// 输入检查
	if (frameLeft.empty() || frameRight.empty())
	{
		disparity = cv::Scalar(0);
		return 0;
	}
	if (m_frameWidth == 0 || m_frameHeight == 0)
	{
		if (init(frameLeft.cols, frameLeft.rows, "calib_paras.xml"/*待改为由本地设置文件确定*/,"remap.xml") == 0)	//执行类初始化
		{
			return 0;
		}
	}

	// 转换为灰度图
	cv::Mat img1proc, img2proc;
	cvtColor(frameLeft, img1proc, CV_BGR2GRAY);
	cvtColor(frameRight, img2proc, CV_BGR2GRAY);

	// 校正图像，使左右视图行对齐	
	cv::Mat img1remap, img2remap;
	if (m_Calib_Data_Loaded)
	{
		remap(img1proc, img1remap, m_Calib_Mat_Remap_X_L, m_Calib_Mat_Remap_Y_L, cv::INTER_LINEAR);		// 对用于视差计算的画面进行校正
		remap(img2proc, img2remap, m_Calib_Mat_Remap_X_R, m_Calib_Mat_Remap_Y_R, cv::INTER_LINEAR);
	}
	else
	{
		img1remap = img1proc;
		img2remap = img2proc;
	}

	// 对左右视图的左边进行边界延拓，以获取与原始视图相同大小的有效视差区域
	cv::Mat img1border, img2border;
	if (m_numberOfDisparies != m_BM.state->numberOfDisparities)
		m_numberOfDisparies = m_BM.state->numberOfDisparities;
	copyMakeBorder(img1remap, img1border, 0, 0, m_BM.state->numberOfDisparities, 0, IPL_BORDER_REPLICATE);
	copyMakeBorder(img2remap, img2border, 0, 0, m_BM.state->numberOfDisparities, 0, IPL_BORDER_REPLICATE);

	// 计算视差
	cv::Mat dispBorder;
	m_BM(img1border, img2border, dispBorder);

	// 截取与原始画面对应的视差区域（舍去加宽的部分）
	cv::Mat disp;
	disp = dispBorder.colRange(m_BM.state->numberOfDisparities, img1border.cols);
	disp.copyTo(disparity, m_Calib_Mat_Mask_Roi);

	// 输出处理后的图像
	if (m_Calib_Data_Loaded)
		remap(frameLeft, imageLeft, m_Calib_Mat_Remap_X_L, m_Calib_Mat_Remap_Y_L, cv::INTER_LINEAR);
	else
		frameLeft.copyTo(imageLeft);
	rectangle(imageLeft, m_Calib_Roi_L, CV_RGB(0, 0, 255), 3);

	if (m_Calib_Data_Loaded)
		remap(frameRight, imageRight, m_Calib_Mat_Remap_X_R, m_Calib_Mat_Remap_Y_R, cv::INTER_LINEAR);
	else
		frameRight.copyTo(imageRight);
	rectangle(imageRight, m_Calib_Roi_R, CV_RGB(0, 0, 255), 3);

	return 1;
}


/*----------------------------
* 功能 : 基于 SGBM 算法计算视差
*----------------------------
* 函数 : StereoMatch::sgbmMatch
* 访问 : public
* 返回 : 0 - 失败，1 - 成功
*
* 参数 : frameLeft		[in]	左摄像机帧图
* 参数 : frameRight		[in]	右摄像机帧图
* 参数 : disparity		[out]	视差图
* 参数 : imageLeft		[out]	处理后的左视图，用于显示
* 参数 : imageRight		[out]	处理后的右视图，用于显示
*/
int StereoMatch::sgbmMatch(cv::Mat& frameLeft, cv::Mat& frameRight, cv::Mat& disparity, cv::Mat& imageLeft, cv::Mat& imageRight)
{
	// 输入检查
	if (frameLeft.empty() || frameRight.empty())
	{
		disparity = cv::Scalar(0);
		return 0;
	}
	if (m_frameWidth == 0 || m_frameHeight == 0)
	{
		if (init(frameLeft.cols, frameLeft.rows, "calib_paras.xml"/*待改为由本地设置文件确定*/, "remap.xml") == 0)	//执行类初始化
		{
			return 0;
		}
	}

	// 复制图像
	cv::Mat img1proc, img2proc;
	frameLeft.copyTo(img1proc);
	frameRight.copyTo(img2proc);

	// 校正图像，使左右视图行对齐	
	cv::Mat img1remap, img2remap;
	if (m_Calib_Data_Loaded)
	{
		remap(img1proc, img1remap, m_Calib_Mat_Remap_X_L, m_Calib_Mat_Remap_Y_L, cv::INTER_LINEAR);		// 对用于视差计算的画面进行校正
		remap(img2proc, img2remap, m_Calib_Mat_Remap_X_R, m_Calib_Mat_Remap_Y_R, cv::INTER_LINEAR);
	}
	else
	{
		img1remap = img1proc;
		img2remap = img2proc;
	}

	// 对左右视图的左边进行边界延拓，以获取与原始视图相同大小的有效视差区域
	cv::Mat img1border, img2border;
	if (m_numberOfDisparies != m_SGBM.numberOfDisparities)
		m_numberOfDisparies = m_SGBM.numberOfDisparities;
	copyMakeBorder(img1remap, img1border, 0, 0, m_SGBM.numberOfDisparities, 0, IPL_BORDER_REPLICATE);
	copyMakeBorder(img2remap, img2border, 0, 0, m_SGBM.numberOfDisparities, 0, IPL_BORDER_REPLICATE);

	// 计算视差
	cv::Mat dispBorder;
	m_SGBM(img1border, img2border, dispBorder);

	// 截取与原始画面对应的视差区域（舍去加宽的部分）
	cv::Mat disp;
	disp = dispBorder.colRange(m_SGBM.numberOfDisparities, img1border.cols);
	disp.copyTo(disparity, m_Calib_Mat_Mask_Roi);

	// 输出处理后的图像
	imageLeft = img1remap.clone();
	imageRight = img2remap.clone();
	rectangle(imageLeft, m_Calib_Roi_L, CV_RGB(0, 255, 0), 3);
	rectangle(imageRight, m_Calib_Roi_R, CV_RGB(0, 255, 0), 3);

	return 1;
}
int StereoMatch::stereoFeatureMatch(cv::Mat& frameLeft, cv::Mat& frameRight, Mat& LeftMap1, Mat& LeftMap2, Mat& RightMap1, Mat& RightMap2, Mat& m_LeftH, Mat& m_RightH,Mat& h,int siftOrSurf)
{
	shared_ptr<Pattern> leftPattern = make_shared<Pattern>(frameLeft);
	shared_ptr<Pattern> rightPattern = make_shared<Pattern>(frameRight);
	vector<DMatch> matches;

	shared_ptr<DescriptorMatcher> matcher = make_shared<BFMatcher>(NORM_L2);
	FeatureMatch fm(leftPattern, rightPattern, matcher
			, siftOrSurf);//1为surf,0为sift
	//ft.match(matches);
	fm.knnMatch(matches);
	Mat F;
	fm.refineMatchesWithFundmentalMatrix(matches, F, 1.0, 0.99);

	fm.refineMatcheswithHomography(matches, 3.0, h);

	cout << "Fundmental Matric:" << endl;
	cout << F << endl;

	cout << "Homography Matric:" << endl;
	cout << h << endl;


	Mat outImg;
	drawMatches(leftPattern->image, leftPattern->keypoints, rightPattern->image, rightPattern->keypoints, matches, outImg);
	resize(outImg, outImg,Size(640, 480));
	imshow("Matches", outImg);

	vector<Point2f> m_LeftInlier;
	vector<Point2f> m_RightInlier;
	m_LeftInlier.resize(leftPattern->keypoints.size());
	m_RightInlier.resize(rightPattern->keypoints.size());
	for (int i = 0; i<leftPattern->keypoints.size(); i++)
	{
		m_LeftInlier[i].x = leftPattern->keypoints[i].pt.x;
		m_LeftInlier[i].y = leftPattern->keypoints[i].pt.y;
		m_RightInlier[i].x = rightPattern->keypoints[i].pt.x;
		m_RightInlier[i].y = rightPattern->keypoints[i].pt.y;
	}

	//Mat m_LeftH;
	//Mat m_RightH;
	stereoRectifyUncalibrated(Mat(m_LeftInlier), Mat(m_RightInlier), F,
		Size(frameLeft.cols, frameLeft.rows),
		m_LeftH, m_RightH, 5);

	// 任意指定一个内参数矩阵K，不会影响计算结果，此处设为单位阵。
	Mat K = Mat::eye(3, 3, CV_64F); // 注意一定是double类型
	Mat invK = K.inv(DECOMP_SVD);
	Mat LeftR = invK*m_LeftH*K;     // 根据单应变换矩阵计算左图摄像机在空间中的变换矩阵R1
	Mat RightR = invK*m_RightH*K;   // 计算右图摄像机在空间中的变换矩阵R2

	Mat Distort;                    // 径向畸变为0，设为空矩阵
	Size UndistSize(frameLeft.cols, frameLeft.rows);

	// 计算左右两幅图像的映射矩阵
	initUndistortRectifyMap(K, Distort, LeftR, K, UndistSize, CV_32FC1, LeftMap1, LeftMap2);
	initUndistortRectifyMap(K, Distort, RightR, K, UndistSize, CV_32FC1, RightMap1, RightMap2);


	//Size UndistSize(frameLeft.cols, frameLeft.rows);
	//Mat R1 = cameraMatrix_L.inv(DECOMP_SVD)* m_LeftH * cameraMatrix_L;
	//Mat R2 = cameraMatrix_R.inv(DECOMP_SVD)* m_RightH * cameraMatrix_R;
	//Mat P1 = cameraMatrix_L;
	//Mat P2 = cameraMatrix_R;

	//Mat remapL1, remapL2, remapR1, remapR2;
	////生成图像校正所需的像素映射矩阵
	//initUndistortRectifyMap(
	//	cameraMatrix_L,
	//	distortionCoefficients_L,
	//	R1, P1,
	//	UndistSize,
	//	CV_32FC1,
	//	LeftMap1, LeftMap2);

	//initUndistortRectifyMap(
	//	cameraMatrix_R,
	//	distortionCoefficients_R,
	//	R2, P2,
	//	UndistSize,
	//	CV_32FC1,
	//	RightMap1, RightMap2);

	return 0;
}
int StereoMatch::uncalibratedSgbmMatch(cv::Mat& frameLeft, cv::Mat& frameRight, cv::Mat& disparity,cv::Mat& disparityL, cv::Mat& disparityR, cv::Mat& imageLeft, cv::Mat& imageRight, int writeOrRead,int siftOrSurf)
{
	// 输入检查
	if (frameLeft.empty() || frameRight.empty())
	{
		disparityL = cv::Scalar(0);
		disparityR = cv::Scalar(0);
		return 0;
	}
	Mat imgLeftborder, imgRightborder;

	if (m_numberOfDisparies != m_SGBM.numberOfDisparities)
		m_numberOfDisparies = m_SGBM.numberOfDisparities;

	Mat remapL1, remapL2, remapR1, remapR2;
	Mat m_LeftH;
	Mat m_RightH;
	Mat h;
	if (writeOrRead == 0)
	{
		stereoFeatureMatch(frameLeft, frameRight, 
			remapL1, remapL2, remapR1, remapR2, m_LeftH, m_RightH, h, siftOrSurf);
		cv::FileStorage fs(remapXml, cv::FileStorage::WRITE);
		if (!fs.isOpened())
		{
			return (0);
		}
		fs << "remapX1" << remapL1;
		fs << "remapY1" << remapL2;
		fs << "remapX2" << remapR1;
		fs << "remapY2" << remapR2;
		fs.release();
	}
	else if (writeOrRead = 1)
	{
		cv::FileStorage fs(remapXml, cv::FileStorage::READ);
		if (!fs.isOpened())
		{
			return (0);
		}
		fs["remapX1"] >> remapL1;
		fs["remapY1"] >> remapL2;
		fs["remapX2"] >> remapR1;
		fs["remapY2"] >> remapR2;
		fs.release();
	}
	Mat m_LeftRectyImage, m_RightRectyImage;
	remap(frameLeft, m_LeftRectyImage, remapL1, remapL2, INTER_LINEAR);
	remap(frameRight, m_RightRectyImage, remapR1, remapR2, INTER_LINEAR);
	copyMakeBorder(m_LeftRectyImage, imgLeftborder, 0, 0, m_numberOfDisparies, 0, IPL_BORDER_REPLICATE);
	copyMakeBorder(m_RightRectyImage, imgRightborder, 0, 0, m_numberOfDisparies, 0, IPL_BORDER_REPLICATE);
	// 计算视差
	cv::Mat dispBorder;
	m_SGBM(imgLeftborder, imgRightborder, dispBorder);

	// 截取与原始画面对应的视差区域（舍去加宽的部分）
	cv::Mat disp;
	disp = dispBorder.colRange(m_SGBM.numberOfDisparities, imgLeftborder.cols);
	m_Calib_Mat_Mask_Roi = cv::Mat::zeros(m_frameHeight, m_frameWidth, CV_8UC1);
	//disp.copyTo(disparity, m_Calib_Mat_Mask_Roi);
	disp.copyTo(disparity);

	imgLeftborder.copyTo(imageLeft);
	imgRightborder.copyTo(imageRight);

	Mat LeftMap1, LeftMap2, RightMap1, RightMap2;
	Size UndistSize(frameLeft.cols, frameLeft.rows);
	Mat R1 = cameraMatrix_L.inv(DECOMP_SVD)* m_LeftH.inv() * cameraMatrix_L;
	Mat R2 = cameraMatrix_R.inv(DECOMP_SVD)* m_RightH.inv() * cameraMatrix_R;
	Mat P1 = cameraMatrix_L;
	Mat P2 = cameraMatrix_R;
	//生成图像校正所需的像素映射矩阵
	initUndistortRectifyMap(
		cameraMatrix_L,
		distortionCoefficients_L,
		R1, P1,
		UndistSize,
		CV_32FC1,
		LeftMap1, LeftMap2);

	Mat m_dispRectyImage, m_dispBorderImage, dispRecty;
	remap(disp, m_dispRectyImage, LeftMap1, LeftMap2, INTER_LINEAR);
	copyMakeBorder(m_dispRectyImage, m_dispBorderImage, 0, 0, m_numberOfDisparies, 0, IPL_BORDER_REPLICATE);
	dispRecty = m_dispBorderImage.colRange(m_SGBM.numberOfDisparities, imgLeftborder.cols);
	dispRecty.copyTo(disparityL);
	warpPerspective(disparityL, disparityR, h.inv(), disparityR.size());
	return 0;
}
int StereoMatch::uncalibratedBmMatch(cv::Mat& frameLeft, cv::Mat& frameRight, cv::Mat& disparityL, cv::Mat& disparityR, cv::Mat& imageLeft, cv::Mat& imageRight, int writeOrRead, int siftOrSurf)
{
	// 输入检查
	if (frameLeft.empty() || frameRight.empty())
	{
		disparityL = cv::Scalar(0);
		disparityR = cv::Scalar(0);
		return 0;
	}
	// 转换为灰度图
	cv::Mat imgLeftproc, imgRightproc;
	cvtColor(frameLeft, imgLeftproc, CV_BGR2GRAY);
	cvtColor(frameRight, imgRightproc, CV_BGR2GRAY);
	Mat imgLeftborder, imgRightborder;

	Mat remapL1, remapL2, remapR1, remapR2;
	Mat m_LeftH;
	Mat m_RightH;
	Mat h;
	if (writeOrRead == 0)
	{
		stereoFeatureMatch(frameLeft, frameRight,
			remapL1, remapL2, remapR1, remapR2, m_LeftH, m_RightH, h,siftOrSurf);
		cv::FileStorage fs(remapXml, cv::FileStorage::WRITE);
		if (!fs.isOpened())
		{
			return (0);
		}
		fs << "remapX1" << remapL1;
		fs << "remapY1" << remapL2;
		fs << "remapX2" << remapR1;
		fs << "remapY2" << remapR2;
		fs.release();
	}
	else if (writeOrRead = 1)
	{
		cv::FileStorage fs(remapXml, cv::FileStorage::READ);
		if (!fs.isOpened())
		{
			return (0);
		}
		fs["remapX1"] >> remapL1;
		fs["remapY1"] >> remapL2;
		fs["remapX2"] >> remapR1;
		fs["remapY2"] >> remapR2;
		fs.release();
	}
	Mat m_LeftRectyImage, m_RightRectyImage;
	remap(frameLeft, m_LeftRectyImage, remapL1, remapL2, INTER_LINEAR);
	remap(frameRight, m_RightRectyImage, remapR1, remapR2, INTER_LINEAR);
	copyMakeBorder(m_LeftRectyImage, imgLeftborder, 0, 0, m_numberOfDisparies, 0, IPL_BORDER_REPLICATE);
	copyMakeBorder(m_RightRectyImage, imgRightborder, 0, 0, m_numberOfDisparies, 0, IPL_BORDER_REPLICATE);

	if (m_numberOfDisparies != m_BM.state->numberOfDisparities)
		m_numberOfDisparies = m_BM.state->numberOfDisparities;

	// 计算视差
	cv::Mat dispBorder;
	m_BM(imgLeftborder, imgRightborder, dispBorder);

	// 截取与原始画面对应的视差区域（舍去加宽的部分）
	cv::Mat disp;
	disp = dispBorder.colRange(m_BM.state->numberOfDisparities, imgLeftborder.cols);
	//disp.copyTo(disparity, m_Calib_Mat_Mask_Roi);

	imgLeftborder.copyTo(imageLeft);
	imgRightborder.copyTo(imageRight);

	Mat LeftMap1, LeftMap2, RightMap1, RightMap2;
	Size UndistSize(frameLeft.cols, frameLeft.rows);
	Mat R1 = cameraMatrix_L.inv(DECOMP_SVD)* m_LeftH.inv() * cameraMatrix_L;
	Mat R2 = cameraMatrix_R.inv(DECOMP_SVD)* m_RightH.inv() * cameraMatrix_R;
	Mat P1 = cameraMatrix_L;
	Mat P2 = cameraMatrix_R;
	//生成图像校正所需的像素映射矩阵
	initUndistortRectifyMap(
		cameraMatrix_L,
		distortionCoefficients_L,
		R1, P1,
		UndistSize,
		CV_32FC1,
		LeftMap1, LeftMap2);

	Mat m_dispRectyImage, m_dispBorderImage, dispRecty;
	remap(disp, m_dispRectyImage, LeftMap1, LeftMap2, INTER_LINEAR);
	copyMakeBorder(m_dispRectyImage, m_dispBorderImage, 0, 0, m_numberOfDisparies, 0, IPL_BORDER_REPLICATE);
	dispRecty = m_dispBorderImage.colRange(m_SGBM.numberOfDisparities, imgLeftborder.cols);
	dispRecty.copyTo(disparityL);
	warpPerspective(disparityL, disparityR, h.inv(), disparityR.size());
	return 0;
}

/*----------------------------
* 功能 : 计算三维点云
*----------------------------
* 函数 : StereoMatch::getPointClouds
* 访问 : public
* 返回 : 0 - 失败，1 - 成功
*
* 参数 : disparity		[in]	视差数据
* 参数 : pointClouds	[out]	三维点云
*/
int StereoMatch::getPointClouds(cv::Mat& disparity, cv::Mat& pointClouds)
{
	if (disparity.empty())
	{
		return 0;
	}

	//计算生成三维点云
	cv::reprojectImageTo3D(disparity, pointClouds, m_Calib_Mat_Q, true);

	// 坐标数据校正
	// 乘以 16 得到 毫米 单位坐标，乘以 1.6 得到 厘米 单位坐标
	// 原理参见：http://blog.csdn.net/chenyusiyuan/article/details/5967291 
	pointClouds *= 1.6;

	// 校正 Y 方向数据，正负反转
	// 原理参见：http://blog.csdn.net/chenyusiyuan/article/details/5970799 
	for (int y = 0; y < pointClouds.rows; ++y)
	{
		for (int x = 0; x < pointClouds.cols; ++x)
		{
			cv::Point3f point = pointClouds.at<cv::Point3f>(y, x);
			point.y = -point.y;
			pointClouds.at<cv::Point3f>(y, x) = point;
		}
	}

	return 1;
}


/*----------------------------
* 功能 : 获取伪彩色视差图
*----------------------------
* 函数 : StereoMatch::getDisparityImage
* 访问 : public
* 返回 : 0 - 失败，1 - 成功
*
* 参数 : disparity			[in]	原始视差数据
* 参数 : disparityImage		[out]	伪彩色视差图
* 参数 : isColor			[in]	是否采用伪彩色，默认为 true，设为 false 时返回灰度视差图
*/
int StereoMatch::getDisparityImage(cv::Mat& disparity, cv::Mat& disparityImage, bool isColor)
{
	// 将原始视差数据的位深转换为 8 位
	cv::Mat disp8u;
	if (disparity.depth() != CV_8U)
	{
		//disparity.convertTo(disp8u, CV_8U, 255 / (m_numberOfDisparies*16.));
		disparity.convertTo(disp8u, CV_8U);
	}
	else
	{
		disp8u = disparity;
	}

	// 转换为伪彩色图像 或 灰度图像
	if (isColor)
	{
		if (disparityImage.empty() || disparityImage.type() != CV_8UC3)
		{
			disparityImage = cv::Mat::zeros(disparity.rows, disparity.cols, CV_8UC3);
		}

		for (int y = 0; y<disparity.rows; y++)
		{
			for (int x = 0; x<disparity.cols; x++)
			{
				uchar val = disp8u.at<uchar>(y, x);
				uchar r, g, b;

				if (val == 0)
					r = g = b = 0;
				else
				{
					r = 255 - val;
					g = val < 128 ? val * 2 : (uchar)((255 - val) * 2);
					b = val;
				}

				disparityImage.at<cv::Vec3b>(y, x) = cv::Vec3b(r, g, b);
			}
		}
	}
	else
	{
		disp8u.copyTo(disparityImage);
	}

	return 1;
}


/*----------------------------
* 功能 : 保存三维点云到本地 txt 文件
*----------------------------
* 函数 : StereoMatch::savePointClouds
* 访问 : public
* 返回 : void
*
* 参数 : pointClouds	[in]	三维点云数据
* 参数 : filename		[in]	文件路径
*/
void StereoMatch::savePointClouds(cv::Mat& pointClouds, const char* filename)
{
	const double max_z = 1.0e4;
	try
	{
		FILE* fp = fopen(filename, "wt");
		for (int y = 0; y < pointClouds.rows; y++)
		{
			for (int x = 0; x < pointClouds.cols; x++)
			{
				cv::Vec3f point = pointClouds.at<cv::Vec3f>(y, x);
				if (fabs(point[2] - max_z) < FLT_EPSILON || fabs(point[2]) > max_z)
					fprintf(fp, "%d %d %d\n", 0, 0, 0);
				else
					fprintf(fp, "%f %f %f\n", point[0], point[1], point[2]);
			}
		}
		fclose(fp);
	}
	catch (std::exception* e)
	{
		printf("Failed to save point clouds. Error: %s \n\n", e->what());
	}
}

