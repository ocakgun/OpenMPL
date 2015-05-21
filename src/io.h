/*************************************************************************
    > File Name: io.h
    > Author: Yibo Lin
    > Mail: yibolin@utexas.edu
    > Created Time: Thu 06 Nov 2014 08:53:46 AM CST
 ************************************************************************/

#ifndef _SIMPLEMPL_IO_H
#define _SIMPLEMPL_IO_H

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <limits>
#include <map>
#include <algorithm>
#include <numeric>
#include <boost/lexical_cast.hpp>
#include <limbo/parsers/gdsii/stream/GdsReader.h>
#include <limbo/parsers/gdsii/stream/GdsWriter.h>

#include "db.h"

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::ifstream;
using std::ofstream;
using std::numeric_limits;
using std::map;
using std::pair;
using std::make_pair;

namespace gtl = boost::polygon;
using boost::int32_t;
using boost::int64_t;
using boost::array;
using gtl::point_concept;
using gtl::rectangle_concept;
using gtl::polygon_90_concept;
using gtl::polygon_90_set_concept;
using gtl::point_data;
using gtl::rectangle_data;
using gtl::polygon_90_data;
using gtl::polygon_90_set_data;

using namespace gtl::operators;

namespace SimpleMPL {

/// read gds file 
template <typename T>
struct GdsReader : GdsParser::GdsDataBase
{
	typedef T coordinate_type;
	typedef LayoutDB<coordinate_type> layoutdb_type;
	typedef typename layoutdb_type::point_type point_type;
	typedef typename layoutdb_type::rectangle_type rectangle_type;
	typedef typename layoutdb_type::polygon_type polygon_type;
	typedef typename layoutdb_type::polygon_pointer_type polygon_pointer_type;
	typedef typename layoutdb_type::rectangle_pointer_type rectangle_pointer_type;
	typedef typename layoutdb_type::path_type path_type;

	string strname; // TOPCELL name, useful for dump out gds files 
	double unit;
	int32_t layer;
	int32_t status; // 0: not in any block, 1 in BOUNDARY or BOX block, 2 in PATH   
	vector<point_type> vPoint;
	int64_t file_size; // in bytes 

	layoutdb_type& db;  

	GdsReader(layoutdb_type& _db) : db(_db) {}

	bool operator() (string const& filename)  
	{
		// calculate file size 
		ifstream in (filename.c_str());
		if (!in.good()) return false;
		std::streampos begin = in.tellg();
		in.seekg(0, std::ios::end);
		std::streampos end = in.tellg();
		file_size = (end-begin);
		in.close();
		// read gds 
		return GdsParser::read(*this, filename);
	}

	template <typename ContainerType>
	void general_cbk(string const& ascii_record_type, string const& ascii_data_type, ContainerType const& vData)
	{
		if (ascii_record_type == "UNITS")
		{
			unit = vData[1]; 
		}
		else if (ascii_record_type == "BOUNDARY" || ascii_record_type == "BOX")
		{
			vPoint.clear();
			layer = 0;
			status = 1;
		}
		else if (ascii_record_type == "PATH")
		{
			vPoint.clear();
			layer = 0;
			status = 2;
		}
		else if (ascii_record_type == "LAYER")
		{
			layer = vData[0];
		}
		else if (ascii_record_type == "XY")
		{
			if (status == 1 || status == 2)
			{
				assert((vData.size() % 2) == 0 && vData.size() > 4);
				vPoint.clear();
				uint32_t end = vData.size();
				// skip last point for BOX and BOUNDARY
				if (status == 1) end -= 2;
				for (uint32_t i = 0; i < end; i += 2)
					vPoint.push_back(gtl::construct<point_type>(vData[i], vData[i+1]));
			}
		}
		else if (ascii_record_type == "ENDEL")
		{
			if (status == 1)
			{
				assert(layer != -1);

				db.add_polygon(layer, vPoint);

				status = 0;
			}
			else if (status == 2)
			{
				assert(layer != -1);
				db.add_path(layer, vPoint);

				status = 0;
			}
		}
		else if (ascii_record_type == "STRNAME")
		{
			assert(ascii_data_type == "STRING");
			assert(!vData.empty());
			strname.assign(vData.begin(), vData.end());
		}
	}

	// required callbacks in parser 
	virtual void bit_array_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<int> const& vBitArray)
	{this->general_cbk(ascii_record_type, ascii_data_type, vBitArray);}
	virtual void integer_2_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<int> const& vInteger)
	{this->general_cbk(ascii_record_type, ascii_data_type, vInteger);}
	virtual void integer_4_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<int> const& vInteger)
	{this->general_cbk(ascii_record_type, ascii_data_type, vInteger);}
	virtual void real_4_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<double> const& vFloat) 
	{this->general_cbk(ascii_record_type, ascii_data_type, vFloat);}
	virtual void real_8_cbk(const char* ascii_record_type, const char* ascii_data_type, vector<double> const& vFloat) 
	{this->general_cbk(ascii_record_type, ascii_data_type, vFloat);}
	virtual void string_cbk(const char* ascii_record_type, const char* ascii_data_type, string const& str) 
	{this->general_cbk(ascii_record_type, ascii_data_type, str);}
	virtual void begin_end_cbk(const char* ascii_record_type)
	{this->general_cbk(ascii_record_type, "", vector<int>());}

};

/// write gds file 
template <typename T>
struct GdsWriter
{
	typedef T coordinate_type;
	typedef LayoutDB<coordinate_type> layoutdb_type;
	typedef typename layoutdb_type::point_type point_type;
	typedef typename layoutdb_type::rectangle_type rectangle_type;
	typedef typename layoutdb_type::polygon_type polygon_type;
	typedef typename layoutdb_type::polygon_pointer_type polygon_pointer_type;
	typedef typename layoutdb_type::rectangle_pointer_type rectangle_pointer_type;
	typedef typename layoutdb_type::path_type path_type;

	void operator() (string const& filename, layoutdb_type const& db, string const& strname = "TOPCELL") const 
	{
		GdsParser::GdsWriter gw (filename.c_str());
		gw.gds_create_lib("POLYGONS", 0.001 /* um per bit */ );
		gw.gds_write_bgnstr();
		gw.gds_write_strname(strname.c_str());

		// basic operation
		// will add more 
		(*this)(gw, db.hPolygon);

		gw.gds_write_endstr();
		gw.gds_write_endlib(); 
	}
	void operator() (GdsParser::GdsWriter& gw, map<int32_t, vector<polygon_pointer_type > > const& hPolygon) const 
	{
		for (BOOST_AUTO(it, hPolygon.begin()); it != hPolygon.end(); ++it)
		{
			int32_t const& layer = it->first;
			vector<polygon_pointer_type> const& vPolygon = it->second;

			BOOST_FOREACH(polygon_pointer_type const& pPolygon, vPolygon)
			{
				polygon_type const& polygon = *pPolygon;
				vector<coordinate_type> vx, vy;
				vx.reserve(polygon.size());
				vy.reserve(polygon.size());
				for (BOOST_AUTO(itp, polygon.begin()); itp != polygon.end(); ++itp)
				{
					vx.push_back((*itp).x());
					vy.push_back((*itp).y());
				}
				gw.write_boundary(layer, 0, vx, vy, false);
			}
		}
	}
	void operator() (GdsParser::GdsWriter& gw, map<int32_t, vector<rectangle_pointer_type> > const& hRect) const 
	{
		for (BOOST_AUTO(it, hRect.begin()); it != hRect.end(); ++it)
		{
			int32_t const& layer = it->first;
			vector<rectangle_pointer_type> const& vRect = it->second;

			BOOST_FOREACH(rectangle_pointer_type const& pRect, vRect)
			{
				rectangle_type const& rect = *pRect;
				gw.write_box(layer, 0, 
						gtl::xl(rect), gtl::yl(rect), 
						gtl::xh(rect), gtl::yh(rect));
			}
		}
	}
};


/// parse command line arguments 
struct CmdParser
{
	/// required commands 
	string input_gds;
	string output_gds;
	vector<string> vInputRoute;
	/// optional commands 
	int64_t min_spacing;
	int64_t min_width;
	int64_t min_area;
	string log_filename;
	string score_filename;
	int32_t max_iteration;
	pair<string, double> prune_window_hint; ///< hint for window-based pruning 
											///< the first element can be AVERAGE or MAX, the second element is weight
	double round_error;
	double density_budget_lp_variation_weight; ///< weight to balance variation and line hotspot in density_budget_lp()
	double overlay_weight; ///< overlay weight in ILP formulation
	double generate_strategy_no_overlay_area_weight; ///< area weight for 2nd layer sorting cost in UFO::generate_strategy_no_overlay()
	double generate_strategy_large_area_area_weight; ///< area weight for even number of layer sorting cost in UFO::generate_strategy_large_area()
	uint32_t generate_strategy_no_overlay_max_fill_cnt; ///< max number of fills for each layer in each StatWindow during UFO::generate_strategy_no_overlay()

	CmdParser() 
	{
		min_width = 32;
		min_spacing = 32;
		min_area = 4800;
		log_filename = "ufo.log";
		score_filename = "score_s.txt";
		max_iteration = 4;
		prune_window_hint = pair<string, double>("AVERAGE", 1.2);
		round_error = 1e-9;
		density_budget_lp_variation_weight = 1.0;
		overlay_weight = 1;
		generate_strategy_no_overlay_area_weight = 1.0;
		generate_strategy_large_area_area_weight = 1.0;
		generate_strategy_no_overlay_max_fill_cnt = 500;
	}
	bool operator()(int argc, char** argv)
	{
		if (argc < 7) 
		{
			cout << "too few arguments" << endl;
			return false;
		}
		argc--;
		argv++;
		while (argc--)
		{
			if (strcmp(*argv, "-in") == 0)
			{
				argc--;
				argv++;
				input_gds = *argv;
			}
			else if (strcmp(*argv, "-out") == 0)
			{
				argc--;
				argv++;
				output_gds = *argv;
			}
			else if (strcmp(*argv, "-route") == 0)
			{
				argc--;
				argv++;
				vInputRoute.push_back(*argv);
			}
			else if (strcmp(*argv, "-width") == 0)
			{
				argc--;
				argv++;
				min_width = atol(*argv);
			}
			else if (strcmp(*argv, "-spacing") == 0)
			{
				argc--;
				argv++;
				min_spacing = atol(*argv);
			}
			else if (strcmp(*argv, "-area") == 0)
			{
				argc--;
				argv++;
				min_area = atol(*argv);
			}
			else if (strcmp(*argv, "-log") == 0)
			{
				argc--;
				argv++;
				log_filename = *argv;
			}
			else if (strcmp(*argv, "-score") == 0)
			{
				argc--;
				argv++;
				score_filename = *argv;
			}
			else if (strcmp(*argv, "-max_iteration") == 0)
			{
				argc--;
				argv++;
				max_iteration = atoi(*argv);
			}
			else if (strcmp(*argv, "-prune_window_hint") == 0)
			{
				argc--;
				argv++;
				prune_window_hint.first = *argv;
				argc--;
				argv++;
				prune_window_hint.second = atof(*argv);
			}
			else if (strcmp(*argv, "-round_error") == 0)
			{
				argc--;
				argv++;
				round_error = atof(*argv);
			}
			else if (strcmp(*argv, "-overlay_weight") == 0)
			{
				argc--;
				argv++;
				overlay_weight = atof(*argv);
			}
			else 
			{
				cout << "unknown command: " << *argv << endl;
				return false;
			}
			argv++;
		}
		return true;
	}
};

} // namespace SimpleMPL 

#endif 
