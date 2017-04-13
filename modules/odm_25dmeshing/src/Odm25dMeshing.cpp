#include "Odm25dMeshing.hpp"

//We define a vertex_base with info. The "info" (size_t) allow us to keep track of the original point index.
//typedef CGAL::Triangulation_vertex_base_with_info_2<size_t, Kernel> Vb;
//typedef CGAL::Triangulation_data_structure_2<Vb> Tds;
//typedef CGAL::Delaunay_triangulation_2<Kernel, Tds> DT;
//typedef DT::Point cgalPoint;
//typedef DT::Vertex_circulator Vertex_circulator;

typedef CGAL::Projection_traits_xy_3<Kernel> Gt;
typedef CGAL::Triangulation_vertex_base_2<Gt> Vb;
typedef CGAL::Delaunay_mesh_face_base_2<Gt> Fb;
typedef CGAL::Triangulation_data_structure_2<Vb, Fb> Tds;

typedef CGAL::Constrained_Delaunay_triangulation_2<Gt, Tds> CDT;

typedef CGAL::Delaunay_mesh_size_criteria_2<CDT> Criteria;
typedef CGAL::Delaunay_mesher_2<CDT, Criteria> Mesher;

typedef CDT::Vertex_handle Vertex_handle;
typedef CDT::Vertex_circulator Vertex_circulator;
typedef CDT::Point cgalPoint;

// Concurrency
#ifdef CGAL_LINKED_WITH_TBB
typedef CGAL::Parallel_tag Concurrency_tag;
#else
typedef CGAL::Sequential_tag Concurrency_tag;
#endif

int Odm25dMeshing::run(int argc, char **argv) {
	log << logFilePath << "\n";

	// If no arguments were passed, print help and return early.
	if (argc <= 1) {
		printHelp();
		return EXIT_SUCCESS;
	}

	try {

		parseArguments(argc, argv);

//		loadPointCloud();

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
		} else if (argument == "-outliersRemovalPercentage" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) throw Odm25dMeshingException("Argument '" + argument + "' expects 1 more input following it, but no more inputs were provided.");
			std::stringstream ss(argv[argIndex]);
			ss >> outliersRemovalPercentage;
			if (ss.bad()) throw Odm25dMeshingException("Argument '" + argument + "' has a bad value (wrong type).");

			outliersRemovalPercentage = std::min<double>(99.99, std::max<double>(outliersRemovalPercentage, 0));
			log << "Outliers removal was manually set to: " << outliersRemovalPercentage << "\n";
		} else if (argument == "-wlopIterations" && argIndex < argc) {
			++argIndex;
			if (argIndex >= argc) throw Odm25dMeshingException("Argument '" + argument + "' expects 1 more input following it, but no more inputs were provided.");
			std::stringstream ss(argv[argIndex]);
			ss >> wlopIterations;
			if (ss.bad()) throw Odm25dMeshingException("Argument '" + argument + "' has a bad value (wrong type).");

			wlopIterations = std::min<unsigned int>(1000, std::max<unsigned int>(wlopIterations, 1));
			log << "WLOP iterations was manually set to: " << wlopIterations << "\n";
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

void Odm25dMeshing::loadPointCloud(){
	  PlyInterpreter interpreter(points);

	  std::ifstream in(inputFile);
	  if (!in || !CGAL::read_ply_custom_points (in, interpreter, Kernel())){
		  throw Odm25dMeshingException(
		  				"Error when reading points and normals from:\n" + inputFile + "\n");
	  }

	  flipFaces = interpreter.flip_faces();

	  log << "Successfully loaded " << points.size() << " points from file\n";
}

void Odm25dMeshing::buildMesh(){
//	const unsigned int NEIGHBORS = 24;
//
//	if (outliersRemovalPercentage > 0){
//		size_t pointCountBeforeOutRemoval = points.size();
//		log << "Removing outliers\n";
//
//		points.erase(CGAL::remove_outliers(points.begin(), points.end(),
//				 CGAL::First_of_pair_property_map<Pwn>(), NEIGHBORS, outliersRemovalPercentage),
//				points.end());
//		std::vector<Pwn>(points).swap(points);
//
//		log << "Removed " << (pointCountBeforeOutRemoval - points.size()) << " points\n";
//	}
//
//	// Filter points by normal orientation
//	log << "Applying normal filtering\n";
//
//	double COSINE_LIMIT = std::cos(M_PI / 4); // ~ 45 degrees
//
//	Vector3 up(0, 0, 1);
//	if (flipFaces) up = Vector3(0, 0, -1);
//
//	size_t pointCountBeforeNormalFilter = points.size();
//
//	points.erase(std::remove_if(points.begin(), points.end(),[&](Pwn pointWithNormal){
//		double cosine = pointWithNormal.second * up / CGAL::sqrt(pointWithNormal.second*pointWithNormal.second) / CGAL::sqrt(up * up);
//		return cosine <= COSINE_LIMIT;
//	}), points.end());
//
	size_t pointCount = points.size();
//
//	log << "Removed " << (pointCountBeforeNormalFilter - pointCount) << " points\n";
//
//	const double RETAIN_PERCENTAGE = std::min<double>(((100. * (double)maxVertexCount) / (double)pointCount), 80.);   // percentage of points to retain.
	std::vector<Point3> simplifiedPoints;
//
//	log << "Computing average spacing... ";
//
//	FT avgSpacing = CGAL::compute_average_spacing<Concurrency_tag>(
//			points.begin(),
//			points.end(),
//			CGAL::First_of_pair_property_map<Pwn>(),
//			NEIGHBORS);
//
//	log << avgSpacing << "\n";
//
//	log << "Performing weighted locally optimal projection simplification and regularization (retain: " << RETAIN_PERCENTAGE << "%, iterate: " << wlopIterations << ")" << "\n";
//
//	CGAL::wlop_simplify_and_regularize_point_set<Concurrency_tag>(
//		 	points.begin(),
//			points.end(),
//			std::back_inserter(simplifiedPoints),
//			CGAL::First_of_pair_property_map<Pwn>(),
//			RETAIN_PERCENTAGE,
//			8 * avgSpacing,
//			wlopIterations,
//			false); // require_uniform_sampling

//	for (size_t c = 0; c < pointCount; c++){
//		simplifiedPoints.push_back(points[c].first);
//	}

//	std::ofstream f("out.bin");
//	f << simplifiedPoints.size() * 3 << " ";
//
//	for (size_t i = 0; i < simplifiedPoints.size(); i++){
//		f << simplifiedPoints[i].x() << " ";
//		f << simplifiedPoints[i].y() << " ";
//		f << simplifiedPoints[i].z() << " ";
//	}
//	f.close();
//	exit(1);

	std::ifstream fin("out.bin");
	pointCount = 0;
	fin >> pointCount;
	for (size_t i = 0; i < pointCount / 3; i++){
		float x, y, z;
		fin >> x;
		fin >> y;
		fin >> z;
		simplifiedPoints.push_back(Point3(x, y, z));
	}

	pointCount = simplifiedPoints.size();

	if (pointCount < 3){
		throw Odm25dMeshingException("Not enough points");
	}

	log << "Vertex count is " << pointCount << "\n";

//	std::vector< std::pair<cgalPoint, size_t > > pts;
//	try{
//		pts.reserve(pointCount);
//	} catch (const std::bad_alloc&){
//		throw Odm25dMeshingException("Not enough memory");
//	}
//
//	for (size_t i = 0; i < pointCount; ++i){
//		pts.push_back(std::make_pair(cgalPoint(simplifiedPoints[i].x(), simplifiedPoints[i].y()), i));
//	}

	log << "Computing delaunay triangulation\n";

	//The delaunay triangulation is built according to the 2D point cloud
//	DT dt(pts.begin(), pts.end());

	unsigned int numberOfTriangles = static_cast<unsigned >(dt.number_of_faces());
	unsigned int triIndexes = dt.number_of_faces()*3;

	if (numberOfTriangles == 0) throw Odm25dMeshingException("No triangles in resulting mesh");

	log << "Computed " << numberOfTriangles << " triangles\n";

	// Convert to tinyply format
	std::vector<float> vertices;
	std::vector<int> vertexIndicies;

	try{
		vertices.reserve(pointCount);
		vertexIndicies.reserve(triIndexes);
	} catch (const std::bad_alloc&){
		throw Odm25dMeshingException("Not enough memory");
	}


	for (size_t i = 0; i < pointCount; ++i){
		vertices.push_back(simplifiedPoints[i].x());
		vertices.push_back(simplifiedPoints[i].y());
		vertices.push_back(simplifiedPoints[i].z());
	}

	for (DT::Face_iterator face = dt.faces_begin(); face != dt.faces_end(); ++face) {
		if (flipFaces){
			vertexIndicies.push_back(face->vertex(2)->info());
			vertexIndicies.push_back(face->vertex(1)->info());
			vertexIndicies.push_back(face->vertex(0)->info());
		}else{
			vertexIndicies.push_back(face->vertex(0)->info());
			vertexIndicies.push_back(face->vertex(1)->info());
			vertexIndicies.push_back(face->vertex(2)->info());
		}
	}

	log << "Removing spikes\n";

	const float THRESHOLD = 0.1;

	std::vector<float> heights;
	unsigned int spikesRemoved = 0;


	for (DT::Vertex_iterator vertex = dt.vertices_begin(); vertex != dt.vertices_end(); ++vertex){
		// Check if the height between this vertex and its
		// incident vertices is greater than THRESHOLD
		Vertex_circulator vc = dt.incident_vertices(vertex), done(vc);

		if (vc != 0){
			float height = vertices[vertex->info() * 3 + 2];
			int threshold_over_count = 0;
			int vertexCount = 0;

			do{
				if (dt.is_infinite(vc)) continue;

				float ivHeight = vertices[vc->info() * 3 + 2];

				if (fabs(height - ivHeight) > THRESHOLD){
					threshold_over_count++;
					heights.push_back(ivHeight);
				}

				vertexCount++;
			}while(++vc != done);

			if (vertexCount == threshold_over_count){
				// Replace the height of the vertex by the median height
				// of its incident vertices
				std::sort(heights.begin(), heights.end());

				vertices[vertex->info() * 3 + 2] = heights[heights.size() / 2];

				spikesRemoved++;
			}

			heights.clear();
		}
	}

	log << "Removed " << spikesRemoved << " spikes\n";

	log << "Saving mesh to file.\n";

	std::filebuf fb;
	fb.open(outputFile, std::ios::out | std::ios::binary);
	std::ostream outputStream(&fb);

	tinyply::PlyFile plyFile;
	plyFile.add_properties_to_element("vertex", {"x", "y", "z"}, vertices);
	plyFile.add_properties_to_element("face", { "vertex_index" }, vertexIndicies, 3, tinyply::PlyProperty::Type::UINT8);

	plyFile.write(outputStream, false);
	fb.close();

	log << "Successfully wrote mesh to:\n" << outputFile << "\n";
}

void Odm25dMeshing::printHelp() {
	bool printInCoutPop = log.isPrintingInCout();
	log.setIsPrintingInCout(true);

	log << "Usage: odm_25dmeshing -inputFile [plyFile] [optional-parameters]\n";
	log << "Create a 2.5D mesh from an oriented point cloud (points with normals) using a constrained delaunay triangulation. "
		<< "The program requires a path to an input PLY point cloud file, all other input parameters are optional.\n\n";

	log << "	-inputFile	<path>	to PLY point cloud\n"
		<< "	-outputFile	<path>	where the output PLY 2.5D mesh should be saved (default: " << outputFile << ")\n"
		<< "	-logFile	<path>	log file path (default: " << logFilePath << ")\n"
		<< "	-verbose	whether to print verbose output (default: " << (printInCoutPop ? "true" : "false") << ")\n"
		<< "	-outliersRemovalPercentage	<0 - 99.99>	percentage of outliers to remove. Set to 0 to disable. (default: " << outliersRemovalPercentage << ")\n"
		<< "	-maxVertexCount	<0 - N>	Maximum number of vertices in the output mesh. The mesh might have fewer vertices, but will not exceed this limit. (default: " << maxVertexCount << ")\n"
		<< "	-wlopIterations	<1 - 1000>	Iterations of the Weighted Locally Optimal Projection (WLOP) simplification algorithm. Higher values take longer but produce a smoother mesh. (default: " << wlopIterations << ")\n"

		<< "\n";

	log.setIsPrintingInCout(printInCoutPop);
}



