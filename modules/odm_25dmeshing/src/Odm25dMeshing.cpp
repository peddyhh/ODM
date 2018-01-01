#include "Odm25dMeshing.hpp"

int Odm25dMeshing::run(int argc, char **argv) {
	log << logFilePath << "\n";

	// If no arguments were passed, print help and return early.
	if (argc <= 1) {
		printHelp();
		return EXIT_SUCCESS;
	}

	try {

		parseArguments(argc, argv);

		loadPointCloud();

		buildMesh();

	} catch (const Odm25dMeshingException& e) {
		log.setIsPrintingInCout(true);
		log << e.what() << "\n";
		log.printToFile(logFilePath);
		log << "For more detailed information, see log file." << "\n";
		return EXIT_FAILURE;
	} catch (const std::exception& e) {
		log.setIsPrintingInCout(true);
		log << "Error in OdmMeshing:\n";
		log << e.what() << "\n";
		log.printToFile(logFilePath);
		log << "For more detailed information, see log file." << "\n";
		return EXIT_FAILURE;
	}

	log.printToFile(logFilePath);

	return EXIT_SUCCESS;
}

void Odm25dMeshing::loadPointCloud() {
	pcl::PCLPointCloud2 blob;

	log << "Loading point cloud... ";

	if (pcl::io::loadPLYFile(inputFile.c_str(), blob) == -1) {
		throw Odm25dMeshingException("Error when reading from: " + inputFile);
	}

	log << "OK\n";

	log << "Scanning fields... ";

	pcl::PCLPointField *posX = NULL, *posY = NULL, *posZ = NULL;

#define ASSIGN(_name, _field) if (blob.fields[i].name == _name){ _field = &blob.fields[i]; log << _name << " "; continue; }

	for (size_t i = 0; i < blob.fields.size(); ++i) {
		ASSIGN("x", posX);
		ASSIGN("y", posY);
		ASSIGN("z", posZ);
	}

	log << "OK\n";

	if (posX == NULL || posY == NULL || posZ == NULL)
		throw Odm25dMeshingException(
				"Position attributes (x,y,z) missing from input");
	if (posX->datatype != pcl::PCLPointField::FLOAT32
			&& posX->datatype != pcl::PCLPointField::FLOAT64)
		throw Odm25dMeshingException(
				"Only float and float64 types are supported for position information");


	for (size_t point_step = 0, i = 0; point_step < blob.data.size();
			point_step += blob.point_step, i++) {
		uint8_t *point = blob.data.data() + point_step;
		double x,y,z;

		if (posX->datatype == pcl::PCLPointField::FLOAT64) {
			x = *(reinterpret_cast<double *>(point + posX->offset));
			y = *(reinterpret_cast<double *>(point + posY->offset));
			z = *(reinterpret_cast<double *>(point + posZ->offset));
		} else if (posX->datatype == pcl::PCLPointField::FLOAT32) {
			x = *(reinterpret_cast<float *>(point + posX->offset));
			y = *(reinterpret_cast<float *>(point + posY->offset));
			z = *(reinterpret_cast<float *>(point + posZ->offset));
		} else {
			throw Odm25dMeshingException(
					"Invalid datatype " + std::to_string(posX->datatype)  + " for point.");
		}

		points->InsertNextPoint(x, y, z);
	}

	log << "Loaded " << points->GetNumberOfPoints() << " points\n";
}

void Odm25dMeshing::buildMesh(){
	vtkThreadedImageAlgorithm::SetGlobalDefaultEnableSMP(true);

	log << "Remove outliers... ";

	vtkSmartPointer<vtkPolyData> polyPoints =
		  vtkSmartPointer<vtkPolyData>::New();
	polyPoints->SetPoints(points);

	vtkSmartPointer<vtkStaticPointLocator> pointsLocator =
			vtkSmartPointer<vtkStaticPointLocator>::New();
	pointsLocator->SetDataSet(polyPoints);
	pointsLocator->BuildLocator();

	vtkSmartPointer<vtkStatisticalOutlierRemoval> removal =
			vtkSmartPointer<vtkStatisticalOutlierRemoval>::New();
	removal->SetInputData(polyPoints);
	removal->SetLocator(pointsLocator);
	removal->SetSampleSize(24);
	removal->SetStandardDeviationFactor(1.5);
	removal->GenerateOutliersOff();
	removal->Update();

	log << removal->GetNumberOfPointsRemoved() << " points removed\n";

	log << "Squash point cloud to plane... ";

	vtkSmartPointer<vtkPoints> cleanedPoints = removal->GetOutput()->GetPoints();
	vtkSmartPointer<vtkFloatArray> elevation = vtkSmartPointer<vtkFloatArray>::New();
	elevation->SetName("elevation");
	elevation->SetNumberOfComponents(1);
	double p[2];

	for (vtkIdType i = 0; i < cleanedPoints->GetNumberOfPoints(); i++){
		cleanedPoints->GetPoint(i, p);
		elevation->InsertNextValue(p[2]);
		p[2] = 0.0;
		cleanedPoints->SetPoint(i, p);
	}

	log << "OK\n";

	vtkSmartPointer<vtkPolyData> polydataToProcess =
	  vtkSmartPointer<vtkPolyData>::New();
	polydataToProcess->SetPoints(cleanedPoints);
	polydataToProcess->GetPointData()->SetScalars(elevation);

	const float NODATA = -9999;

	double *bounds = polydataToProcess->GetBounds();
	double *center = polydataToProcess->GetCenter();

	double extentX = bounds[1] - bounds[0];
	double extentY = bounds[3] - bounds[2];

	int width = ceil(extentX * resolution);
	int height = ceil(extentY * resolution);

	log << "Plane extentX: " << extentX <<
				", extentY: " << extentY << "\n";

	vtkSmartPointer<vtkPlaneSource> plane =
			vtkSmartPointer<vtkPlaneSource>::New();
	plane->SetResolution(width, height);
	plane->SetOrigin(0.0, 0.0, 0.0);
	plane->SetPoint1(extentX, 0.0, 0.0);
	plane->SetPoint2(0.0, extentY, 0);
	plane->SetCenter(center);
	plane->SetNormal(0.0, 0.0, 1.0);

	vtkSmartPointer<vtkStaticPointLocator> locator =
			vtkSmartPointer<vtkStaticPointLocator>::New();
	locator->SetDataSet(polydataToProcess);
	locator->BuildLocator();

	vtkSmartPointer<vtkShepardKernel> shepardKernel =
				vtkSmartPointer<vtkShepardKernel>::New();
	shepardKernel->SetPowerParameter(2.0);
	shepardKernel->SetKernelFootprintToNClosest();
	shepardKernel->SetNumberOfPoints(shepardNeighbors);

	vtkSmartPointer<vtkImageData> image =
	    vtkSmartPointer<vtkImageData>::New();
	image->SetDimensions(width, height, 1);
	log << "DSM size is " << width << "x" << height << " (" << ceil(width * height * sizeof(float) * 1e-6) << " MB) \n";
	image->AllocateScalars(VTK_FLOAT, 1);

	log << "Point interpolation using shepard's kernel...";

	vtkSmartPointer<vtkPointInterpolator> interpolator =
				vtkSmartPointer<vtkPointInterpolator>::New();
	interpolator->SetInputConnection(plane->GetOutputPort());
	interpolator->SetSourceData(polydataToProcess);
	interpolator->SetKernel(shepardKernel);
	interpolator->SetLocator(locator);
	interpolator->SetNullValue(NODATA);
	interpolator->Update();

	vtkSmartPointer<vtkPolyData> interpolatedPoly =
			interpolator->GetPolyDataOutput();

	vtkSmartPointer<vtkFloatArray> interpolatedElevation =
		  vtkFloatArray::SafeDownCast(interpolatedPoly->GetPointData()->GetArray("elevation"));

	for (int i = 0; i < width; i++){
		for (int j = 0; j < height; j++){
			float* pixel = static_cast<float*>(image->GetScalarPointer(i,j,0));
			vtkIdType cellId = interpolatedPoly->GetCell(j * width + i)->GetPointId(0);
			pixel[0] = interpolatedElevation->GetValue(cellId);
		}
	}

	log << "OK\n";


	if (outputDsmFile != ""){
		log << "Saving DSM to file... ";
		vtkSmartPointer<vtkTIFFWriter> tiffWriter =
				vtkSmartPointer<vtkTIFFWriter>::New();
		tiffWriter->SetFileName(outputDsmFile.c_str());
		tiffWriter->SetInputData(image);
		tiffWriter->Write();
		log << "OK\n";
	}

	vtkSmartPointer<vtkImageAnisotropicDiffusion2D> surfaceDiffusion =
			vtkSmartPointer<vtkImageAnisotropicDiffusion2D>::New();
	surfaceDiffusion->SetInputData(image);
	surfaceDiffusion->FacesOn();
	surfaceDiffusion->EdgesOn();
	surfaceDiffusion->CornersOn();
	surfaceDiffusion->SetDiffusionFactor(1); // Full strength
	surfaceDiffusion->GradientMagnitudeThresholdOn();
	surfaceDiffusion->SetDiffusionThreshold(0.2); // Don't smooth jumps in elevation > than 0.20m
	surfaceDiffusion->SetNumberOfIterations(resolution / 2.0);
	surfaceDiffusion->Update();

	log << "Triangulate... ";

	vtkSmartPointer<vtkGreedyTerrainDecimation> terrain =
			vtkSmartPointer<vtkGreedyTerrainDecimation>::New();
	terrain->SetErrorMeasureToNumberOfTriangles();
	terrain->SetNumberOfTriangles(maxVertexCount * 2); // Approximate
	terrain->SetInputData(surfaceDiffusion->GetOutput());
	terrain->BoundaryVertexDeletionOn();
	terrain->Update();

	log << "OK\nTransform... ";
	vtkSmartPointer<vtkTransform> transform =
			vtkSmartPointer<vtkTransform>::New();
	transform->Translate(-extentX / 2.0 + center[0],
			-extentY / 2.0 + center[1], 0);
	transform->Scale(extentX / width, extentY / height, 1);

	vtkSmartPointer<vtkTransformFilter> transformFilter =
	    vtkSmartPointer<vtkTransformFilter>::New();
	transformFilter->SetInputConnection(terrain->GetOutputPort());
	transformFilter->SetTransform(transform);

	log << "OK\n";

	log << "Saving mesh to file... ";

	vtkSmartPointer<vtkPLYWriter> plyWriter =
			vtkSmartPointer<vtkPLYWriter>::New();
	plyWriter->SetFileName(outputFile.c_str());
	plyWriter->SetInputConnection(transformFilter->GetOutputPort());
	plyWriter->SetFileTypeToASCII();
	plyWriter->Write();

	log << "OK\n";

#ifdef SUPPORTDEBUGWINDOW
	if (showDebugWindow){
		vtkSmartPointer<vtkPolyDataMapper> mapper =
				vtkSmartPointer<vtkPolyDataMapper>::New();
		mapper->SetInputConnection(transformFilter->GetOutputPort());
		mapper->SetScalarRange(150, 170);

	//	  vtkSmartPointer<vtkDataSetMapper> mapper =
	//	    vtkSmartPointer<vtkDataSetMapper>::New();
	//	  mapper->SetInputData(image);
	//	  mapper->SetScalarRange(150, 170);

		  vtkSmartPointer<vtkActor> actor =
			vtkSmartPointer<vtkActor>::New();
		  actor->SetMapper(mapper);
		  actor->GetProperty()->SetPointSize(5);

		  vtkSmartPointer<vtkRenderer> renderer =
			vtkSmartPointer<vtkRenderer>::New();
		  vtkSmartPointer<vtkRenderWindow> renderWindow =
			vtkSmartPointer<vtkRenderWindow>::New();
		  renderWindow->AddRenderer(renderer);
		  vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor =
			vtkSmartPointer<vtkRenderWindowInteractor>::New();
		  renderWindowInteractor->SetRenderWindow(renderWindow);

		  renderer->AddActor(actor);
		  renderer->SetBackground(0.1804,0.5451,0.3412); // Sea green

		  renderWindow->Render();
		  renderWindowInteractor->Start();
	}
#endif
}

void Odm25dMeshing::parseArguments(int argc, char **argv) {
	for (int argIndex = 1; argIndex < argc; ++argIndex) {
		// The argument to be parsed.
		std::string argument = std::string(argv[argIndex]);

		if (argument == "-help") {
			printHelp();
			exit(0);
		} else if (argument == "-verbose") {
			log.setIsPrintingInCout(true);
		} else if (argument == "-maxVertexCount" && argIndex < argc) {
            ++argIndex;
            if (argIndex >= argc) throw Odm25dMeshingException("Argument '" + argument + "' expects 1 more input following it, but no more inputs were provided.");
            std::stringstream ss(argv[argIndex]);
            ss >> maxVertexCount;
            if (ss.bad()) throw Odm25dMeshingException("Argument '" + argument + "' has a bad value (wrong type).");
            maxVertexCount = std::max<unsigned int>(maxVertexCount, 0);
            log << "Vertex count was manually set to: " << maxVertexCount << "\n";
		} else if (argument == "-resolution" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) throw Odm25dMeshingException("Argument '" + argument + "' expects 1 more input following it, but no more inputs were provided.");
			std::stringstream ss(argv[argIndex]);
			ss >> resolution;
			if (ss.bad()) throw Odm25dMeshingException("Argument '" + argument + "' has a bad value (wrong type).");

			resolution = std::min<double>(100000, std::max<double>(resolution, 0.00001));
			log << "Resolution was manually set to: " << resolution << "\n";
		} else if (argument == "-shepardNeighbors" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) throw Odm25dMeshingException("Argument '" + argument + "' expects 1 more input following it, but no more inputs were provided.");
			std::stringstream ss(argv[argIndex]);
			ss >> shepardNeighbors;
			if (ss.bad()) throw Odm25dMeshingException("Argument '" + argument + "' has a bad value (wrong type).");
			shepardNeighbors = std::min<unsigned int>(1000, std::max<unsigned int>(shepardNeighbors, 1));
			log << "Shepard neighbors was manually set to: " << shepardNeighbors << "\n";
		} else if (argument == "-inputFile" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) {
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' expects 1 more input following it, but no more inputs were provided.");
			}
			inputFile = std::string(argv[argIndex]);
			std::ifstream testFile(inputFile.c_str(), std::ios::binary);
			if (!testFile.is_open()) {
				throw Odm25dMeshingException(
						"Argument '" + argument	+ "' has a bad value. (file not accessible)");
			}
			testFile.close();
			log << "Reading point cloud at: " << inputFile << "\n";
		} else if (argument == "-outputFile" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) {
				throw Odm25dMeshingException(
						"Argument '" + argument + "' expects 1 more input following it, but no more inputs were provided.");
			}
			outputFile = std::string(argv[argIndex]);
			std::ofstream testFile(outputFile.c_str());
			if (!testFile.is_open()) {
				throw Odm25dMeshingException(
						"Argument '" + argument + "' has a bad value.");
			}
			testFile.close();
			log << "Writing output to: " << outputFile << "\n";
		}else if (argument == "-outputDsmFile" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) {
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' expects 1 more input following it, but no more inputs were provided.");
			}
			outputDsmFile = std::string(argv[argIndex]);
			std::ofstream testFile(outputDsmFile.c_str());
			if (!testFile.is_open()) {
				throw Odm25dMeshingException(
						"Argument '" + argument	+ "' has a bad value. (file not accessible)");
			}
			testFile.close();
			log << "Saving DSM output to: " << outputDsmFile << "\n";
		} else if (argument == "-showDebugWindow") {
			showDebugWindow = true;
		} else if (argument == "-logFile" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) {
				throw Odm25dMeshingException(
						"Argument '" + argument
								+ "' expects 1 more input following it, but no more inputs were provided.");
			}
			logFilePath = std::string(argv[argIndex]);
			std::ofstream testFile(outputFile.c_str());
			if (!testFile.is_open()) {
				throw Odm25dMeshingException(
						"Argument '" + argument + "' has a bad value.");
			}
			testFile.close();
			log << "Writing log information to: " << logFilePath << "\n";
		} else {
			printHelp();
			throw Odm25dMeshingException("Unrecognised argument '" + argument + "'");
		}
	}
}

void Odm25dMeshing::printHelp() {
	bool printInCoutPop = log.isPrintingInCout();
	log.setIsPrintingInCout(true);

	log << "Usage: odm_25dmeshing -inputFile [plyFile] [optional-parameters]\n";
	log << "Create a 2.5D mesh from a point cloud. "
		<< "The program requires a path to an input PLY point cloud file, all other input parameters are optional.\n\n";

	log << "	-inputFile	<path>	to PLY point cloud\n"
		<< "	-outputFile	<path>	where the output PLY 2.5D mesh should be saved (default: " << outputFile << ")\n"
		<< "	-outputDsmFile	<path>	Optionally output the Digital Surface Model (DSM) computed for generating the mesh. (default: " << outputDsmFile << ")\n"
		<< "	-logFile	<path>	log file path (default: " << logFilePath << ")\n"
		<< "	-verbose	whether to print verbose output (default: " << (printInCoutPop ? "true" : "false") << ")\n"
		<< "	-maxVertexCount	<0 - N>	Maximum number of vertices in the output mesh. The mesh might have fewer vertices, but will not exceed this limit. (default: " << maxVertexCount << ")\n"
		<< "	-shepardNeighbors	<1 - 1000>	Number of nearest neighbors to consider when doing shepard's interpolation. Higher values lead to smoother meshes but take longer to process. (default: " << shepardNeighbors << ")\n"
		<< "	-resolution	<1 - N>	Size of the interpolated digital surface model (DSM) used for deriving the 2.5D mesh, expressed in pixels per meter unit. (default: " << resolution << ")\n"

		<< "\n";

	log.setIsPrintingInCout(printInCoutPop);
}



