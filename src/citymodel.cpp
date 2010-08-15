/* -*-c++-*- libcitygml - Copyright (c) 2010 Joachim Pouderoux, BRGM
 *
 * Contributors:
 *  - Manuel Garnier, BRGM - better normal computation
 *
 * This file is part of libcitygml library
 * http://code.google.com/p/libcitygml
 *
 * libcitygml is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libcitygml is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
*/

#ifdef WIN32
#	include <windows.h>
#else
#	define CALLBACK
#endif
#include "GL/glu.h"
#include "citygml.h"

#include <algorithm>
#include <iterator>

///////////////////////////////////////////////////////////////////////////////

namespace citygml
{
	std::ostream& operator<<( std::ostream& os, const Envelope& e ) 
	{
		return os << e.getLowerBound() << " " << e.getUpperBound();
	}

	std::ostream& operator<<( std::ostream& os, const Object& o ) 
	{ 
		return os << o.getId();
	}

	std::ostream& operator<<( std::ostream& os, const citygml::Geometry& s )
	{
		unsigned int count = 0;
		for ( unsigned int i = 0; i < s.size(); i++ )
		{
			os << *s[i];
			count += s[i]->getVertices().size();
		}

		os << "  @ " << s._polygons.size() << " polys [" << count << " vertices]" << std::endl;

		return os;
	}

	std::ostream& operator<<( std::ostream& os, const CityObject& o ) 
	{
		os << o.getType() << ": " << o.getId() << std::endl;
		os << "  Envelope: " << o.getEnvelope() << std::endl;

		std::map< std::string, std::string >::const_iterator it = o._properties.begin();
		while ( it != o._properties.end() )
		{
			os << "  + " << it->first << ": " << it->second << std::endl;
			it++;
		}

		std::vector< Geometry* >::const_iterator itp = o._geometries.begin();
		for ( ; itp != o._geometries.end(); itp++ ) 
			os << **itp;

		os << "  * " << o._geometries.size() << " geometries." << std::endl;

		return os;
	}

	std::ostream& operator<<( std::ostream& out, const CityModel& model ) 
	{
		const CityObjectsMap& cityObjectsMap = model.getCityObjectsMap();

		CityObjectsMap::const_iterator it = cityObjectsMap.begin();

		for ( ; it != cityObjectsMap.end(); it++ )

			for ( unsigned int i = 0; i < it->second.size(); i++ ) out << *(it->second[i]);

		out << model.size() << " city objects." << std::endl;	

		return out;
	}

	///////////////////////////////////////////////////////////////////////////////

	TVec3d LinearRing::computeNormal( void ) const
	{
		unsigned int len = size();
		if ( len < 3 ) return TVec3d();

		// Tampieri, F. 1992. Newell's method for computing the plane equation of a polygon. In Graphics Gems III, pp.231-232. 
		TVec3d n( 0., 0., 0. );
		for ( unsigned int i = 0; i < len; i++ )
		{
			const TVec3d& current = _vertices[i];
			const TVec3d& next = _vertices[ ( i + 1 ) % len];

			n.x += ( current.y - next.y ) * ( current.z + next.z );
			n.y += ( current.z - next.z ) * ( current.x + next.x );
			n.z += ( current.x - next.x ) * ( current.y + next.y );
		}
		return n.normal();
	}

	void LinearRing::finish( void )
	{
		// Remove duplicated vertex
		unsigned int len = _vertices.size();
		if ( len < 2 ) return;

		for ( unsigned int i = 0; i < len; i++ )
		{
			if ( ( _vertices[i] - _vertices[ ( i + 1 ) % len ] ).sqrLength() < 0.00000001 )
			{
				_vertices.erase( _vertices.begin() + i );
				finish();
				return;
			}
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	AppearanceManager::~AppearanceManager( void ) 
	{
		for ( unsigned int i = 0; i < _appearances.size(); i++ ) delete _appearances[i];

		std::map<std::string, TexCoords*>::iterator it = _texCoordsMap.begin();
		for ( ; it != _texCoordsMap.end(); it++ ) delete it->second;
	}

	void AppearanceManager::addAppearance( Appearance* app ) 
	{ 
		if ( app ) _appearances.push_back( app ); 
	}

	void AppearanceManager::assignNode( const std::string& nodeid )
	{ 
		_lastId = nodeid; 
		if ( !getAppearance( nodeid ) ) 
		{
			_appearanceMap[ nodeid ] = _appearances[ _appearances.size() - 1 ]; 
		
			if ( _lastCoords ) { assignTexCoords( _lastCoords ); _lastId = ""; }
		}
		else _lastId = "";
	}

	bool AppearanceManager::assignTexCoords( TexCoords* tex ) 
	{ 
		_lastCoords = tex;
		if ( _lastId == "" ) return false;
		_texCoordsMap[ _lastId ] = tex; 
		_lastCoords = 0;
		_lastId = "";
		return true;
	}

	///////////////////////////////////////////////////////////////////////////
	// GLU based polygon tesselator
	class Tesselator 
	{		
	public:
		static Tesselator* getInstance( void ) { if ( !_instance ) _instance = new Tesselator(); return _instance; }
		static void destroy( void ) { delete _instance; _instance = 0; }
		
		void init( unsigned int verticesCount, const TVec3d& normal, GLenum winding_rule = GLU_TESS_WINDING_ODD );

		// Add a new contour - add the exterior ring first, then interiors 
		void addContour( const std::vector<TVec3d>& );

		// Let's tesselate!
		void compute( void );

		// Tesselation result access
		inline const std::vector<TVec3d>& getVertices( void ) const { return _vertices; }
		inline const std::vector<unsigned int>& getIndices( void ) const { return _indices; }

	private:
		Tesselator( void );
		~Tesselator( void );

		typedef void (APIENTRY *GLU_TESS_CALLBACK)();
		static void CALLBACK beginCallback( GLenum, void* );
		static void CALLBACK vertexCallback( GLvoid*, void* );
		static void CALLBACK combineCallback( GLdouble[3], void* [4], GLfloat [4], void** , void* );
		static void CALLBACK endCallback( void* );
		static void CALLBACK errorCallback( GLenum, void* );	

	private:
		static Tesselator* _instance; // singleton
		GLUtesselator *_tobj;
		GLenum  _curMode;

		std::vector<TVec3d> _vertices;		
		std::vector<unsigned int> _indices;

		std::vector<unsigned int> _curIndices;
	};

	///////////////////////////////////////////////////////////////////////////////

	Polygon::~Polygon( void ) 
	{ 
		delete _exteriorRing;
		std::vector< LinearRing* >::const_iterator it = _interiorRings.begin();
		for ( ; it != _interiorRings.end(); it++ ) delete *it;
	}

	TVec3d Polygon::computeNormal( void ) 
	{
#ifndef TESSNORMALS
		if ( !_exteriorRing ) return TVec3d();

		TVec3d normal = _exteriorRing->computeNormal();
#else
		if ( _vertices.empty() || _indices.size() < 3 ) 
		{
			//std::cout << "Warning: Unable to compute normal on polygon " << getId() << "!" << std::endl;
			return TVec3d();
		}

		const TVec3d& p1 = _vertices[_indices[0]];
		const TVec3d& p2 = _vertices[_indices[1]];
		const TVec3d& p3 = _vertices[_indices[2]];

		TVec3d normal = ( ( p2 - p1 ).cross( p3 - p1 ) ).normal();
#endif
		return _negNormal ? -normal : normal;
	}

	void Polygon::tesselate( const TVec3d& normal )
	{
		_indices.clear();

		if ( !_exteriorRing || _exteriorRing->size() < 3 )
		{ 
			mergeRings();
			return;
		}

		// Compute the total number of vertices
		unsigned int vsize = _exteriorRing->size();
		for ( unsigned int i = 0; i < _interiorRings.size(); i++ )
			vsize += _interiorRings[i]->size();

		Tesselator* tess = Tesselator::getInstance();
		tess->init( vsize, normal );

		tess->addContour( _exteriorRing->getVertices() );

		for ( unsigned int i = 0; i < _interiorRings.size(); i++ )
			tess->addContour( _interiorRings[i]->getVertices() ); 

		tess->compute();

		_vertices.reserve( tess->getVertices().size() );
		std::copy( tess->getVertices().begin(), tess->getVertices().end(), std::back_inserter( _vertices ) );

		unsigned int indicesSize = tess->getIndices().size();
		if ( indicesSize > 0 ) 
		{
			_indices.resize( indicesSize );
			memcpy (&_indices[0], &tess->getIndices()[0], indicesSize * sizeof(unsigned int) );
		}

		clearRings();
	}

	void Polygon::mergeRings( void )
	{
		_vertices.reserve( _vertices.size() + _exteriorRing->size() );

		std::copy( _exteriorRing->getVertices().begin(), _exteriorRing->getVertices().end(), std::back_inserter( _vertices ) );

		for ( unsigned int i = 0; i < _interiorRings.size(); i++ )
		{
			_vertices.reserve( _vertices.size() + _interiorRings[i]->size() );

			std::copy( _interiorRings[i]->getVertices().begin(), _interiorRings[i]->getVertices().end(), std::back_inserter( _vertices ) );
		}
		clearRings();
		_indices.clear();

		if ( _vertices.size() < 3 ) return;

		// Create triangles' indices
		int indicesSize = 3 * ( _vertices.size() - 2 );
		if ( indicesSize < 3 ) return;
		_indices.resize( indicesSize );
		for ( int i = 0, p = 0; i < indicesSize - 2; i++, p += 3 )
			for ( unsigned int j = 0; j < 3; j++ )
				_indices[ p + j ] = i + j;
	}

	void Polygon::clearRings( void )
	{
		delete _exteriorRing;
		_exteriorRing = NULL;
		for ( unsigned int i = 0; i < _interiorRings.size(); i++ ) delete _interiorRings[i]; 
		_interiorRings.clear();
	}

	// Merge polygon p into the current polygon
	bool Polygon::merge( Polygon* p )
	{
		if ( !p ) return false;

		if ( p->getAppearance() != getAppearance() ) return false;

		if ( p->getVertices().size() == 0 ) return true;

		// merge vertices
		unsigned int oldVSize = _vertices.size();
		unsigned int pVSize = p->_vertices.size();
		_vertices.resize( oldVSize + pVSize );
		for ( unsigned int i = 0; i < pVSize; i++ )
			_vertices[ oldVSize + i ] = p->_vertices[i];
		p->_vertices.clear();
		
		// merge indices
		{
			unsigned int oldSize = _indices.size();
			unsigned int pSize = p->_indices.size();
			_indices.resize( oldSize + pSize );
			for ( unsigned int i = 0; i < pSize; i++ )
				_indices[ oldSize + i ] = oldVSize + p->_indices[i];
			p->_indices.clear();
		}

		// merge normals
		{
			unsigned int oldSize = _normals.size();
			unsigned int pSize = p->_normals.size();
			_normals.resize( oldSize + pSize );
			for ( unsigned int i = 0; i < pSize; i++ )
				_normals[ oldSize + i ] = p->_normals[i];
			p->_normals.clear();
		}

		// merge texcoords
		{
			unsigned int oldSize = min( _texCoords.size(), oldVSize );
			unsigned int pSize = min( p->_texCoords.size(), pVSize );
			_texCoords.resize( oldSize + pSize );
			for ( unsigned int i = 0; i < pSize; i++ )
				_texCoords[ oldSize + i ] = p->_texCoords[i];
			p->_texCoords.clear();
		}

		// merge ids
		_id = _id + "+" + p->_id;

		return true;
	}

	void Polygon::finish( bool doTesselate ) 
	{
#ifndef TESSNORMALS
		TVec3d normal = computeNormal();
		if ( doTesselate ) tesselate( normal );	else mergeRings();
#else
		if ( doTesselate ) tesselate( TVec3d() );	else mergeRings();
		normal = computeNormal();
#endif
		// Save the normal per point field
		_normals.resize( _vertices.size() );
		for ( unsigned int i = 0; i < _vertices.size(); i++ )
			_normals[i] = TVec3f( (float)normal.x, (float)normal.y, (float)normal.z );
	}

	void Polygon::finish( AppearanceManager& appearanceManager, Appearance* defAppearance )
	{
		if ( !appearanceManager.getTexCoords( getId(), _texCoords ) )
			appearanceManager.getTexCoords( _geometry->getId(), _texCoords );
		
		_texCoords.resize( _vertices.size() );
		
		_appearance = appearanceManager.getAppearance( getId() );

		if ( !_appearance ) _appearance = defAppearance;
	}

	void Polygon::addRing( LinearRing* ring ) 
	{
		ring->finish();
		if ( ring->isExterior() ) _exteriorRing = ring;
		else _interiorRings.push_back( ring );
	}

	///////////////////////////////////////////////////////////////////////////////

	Geometry::~Geometry() 
	{ 
		std::vector< Polygon* >::const_iterator it = _polygons.begin();
		for ( ; it != _polygons.end(); it++ ) delete *it;
	}

	void Geometry::addPolygon( Polygon* p ) 
	{ 
		p->_geometry = this;
		_polygons.push_back( p ); 
	}

	void Geometry::finish( AppearanceManager& appearanceManager, Appearance* defAppearance, bool optimize )
	{
		Appearance* myappearance = appearanceManager.getAppearance( getId() );
		std::vector< Polygon* >::const_iterator it = _polygons.begin();
		for ( ; it != _polygons.end(); it++ ) (*it)->finish( appearanceManager, myappearance ? myappearance : defAppearance );

		bool finish = false;
		while ( !finish && optimize ) 
		{
			finish = true;
			
			for ( int i = 0; finish && i < (int)_polygons.size() - 1; i++ ) 
			{
				for ( int j = i+1; finish && j < (int)_polygons.size() - 1; j++ ) 
				{
					if ( !_polygons[i]->merge( _polygons[j] ) ) continue;

					delete _polygons[j];

					//std::cout << " Geom : " << getId() << "... Poly " << _polygons[i]->getId() << " merged with " << _polygons[j]->getId() << std::endl;
					_polygons.erase( _polygons.begin() + j );
					finish = false;		
				}
			}
		}
	}

	bool Geometry::merge( Geometry* g ) 
	{
		if ( !g ) return false;
		if ( g->_lod != _lod || g->_type != _type ) return false;

		unsigned int pSize = g->_polygons.size();
		for ( unsigned int i = 0; i < pSize; i++ )
			_polygons.push_back( g->_polygons[i] );

		g->_polygons.clear();

		_id = _id + "+" + g->_id;

		return true;
	}

	///////////////////////////////////////////////////////////////////////////////

	std::string getCityObjectsClassName( CityObjectsTypeMask mask )
	{
#define GETCITYNAME( _t_ ) if ( mask & COT_ ## _t_ ) ss << # _t_ << "|";
		std::stringstream ss;
		GETCITYNAME( GenericCityObject );
		GETCITYNAME( Building );
		GETCITYNAME( Room );
		GETCITYNAME( BuildingInstallation );
		GETCITYNAME( BuildingFurniture );
		GETCITYNAME( Door );
		GETCITYNAME( Window );
		GETCITYNAME( CityFurniture );
		GETCITYNAME( Track );
		GETCITYNAME( Road );
		GETCITYNAME( Railway );
		GETCITYNAME( Square );
		GETCITYNAME( PlantCover );
		GETCITYNAME( SolitaryVegetationObject );
		GETCITYNAME( WaterBody );
		GETCITYNAME( TINRelief );
		GETCITYNAME( LandUse );
		GETCITYNAME( Tunnel );
		GETCITYNAME( Bridge );
		GETCITYNAME( BridgeConstructionElement );
		GETCITYNAME( BridgeInstallation );
		GETCITYNAME( BridgePart );
		GETCITYNAME( GenericCityObject );
#undef GETCITYNAME
		std::string s = ss.str();
		if ( s != "" ) s.erase( s.length() - 1, 1 ); // remove the last | char
		return s;
	};

	// std::string tokenizer helper
	std::vector<std::string> tokenize( const std::string& str, const std::string& delimiters )
	{
		std::vector<std::string> tokens;
		std::string::size_type lastPos = str.find_first_not_of( delimiters, 0 );
		std::string::size_type pos = str.find_first_of( delimiters, lastPos );

		while ( std::string::npos != pos || std::string::npos != lastPos )
		{
			tokens.push_back( str.substr( lastPos, pos - lastPos ) );
			lastPos = str.find_first_not_of( delimiters, pos );
			pos = str.find_first_of( delimiters, lastPos );
		}
		return tokens;
	}

	inline bool caseInsensitiveStringCompare( const std::string& str1, const std::string& str2 ) 
	{
		std::string s1( str1 );
		std::transform( s1.begin(), s1.end(), s1.begin(), ::tolower );	
		std::string s2( str2 );
		std::transform( s2.begin(), s2.end(), s2.begin(), ::tolower );
		return ( s1 == s2 );
	}

	CityObjectsTypeMask getCityObjectsTypeMaskFromString( const std::string& stringMask ) 
	{
		CityObjectsTypeMask mask = 0;

		std::vector<std::string> tokens = tokenize( stringMask );

#define COMPARECITYNAMEMASK( _t_ ) {\
	bool neg = ( tokens[i][0] == '~' || tokens[i][0] == '!' );\
	if ( caseInsensitiveStringCompare( #_t_, neg ? tokens[i].substr(1) : tokens[i] ) ) { mask = neg ? ( mask & (~ COT_ ## _t_ )) : ( mask | COT_ ## _t_ );}\
	}

		for ( unsigned int i = 0; i < tokens.size(); i++ ) 
		{
			if ( tokens[i].length() == 0 ) continue;

			COMPARECITYNAMEMASK( GenericCityObject );
			COMPARECITYNAMEMASK( Building );
			COMPARECITYNAMEMASK( Room );
			COMPARECITYNAMEMASK( BuildingInstallation );
			COMPARECITYNAMEMASK( BuildingFurniture );
			COMPARECITYNAMEMASK( Door );
			COMPARECITYNAMEMASK( Window );
			COMPARECITYNAMEMASK( CityFurniture );
			COMPARECITYNAMEMASK( Track );				 
			COMPARECITYNAMEMASK( Road );				 
			COMPARECITYNAMEMASK( Railway );
			COMPARECITYNAMEMASK( Square	);				 
			COMPARECITYNAMEMASK( PlantCover	);
			COMPARECITYNAMEMASK( SolitaryVegetationObject );
			COMPARECITYNAMEMASK( WaterBody );
			COMPARECITYNAMEMASK( TINRelief );					 
			COMPARECITYNAMEMASK( LandUse );
			COMPARECITYNAMEMASK( Tunnel );
			COMPARECITYNAMEMASK( Bridge );
			COMPARECITYNAMEMASK( BridgeConstructionElement );
			COMPARECITYNAMEMASK( BridgeInstallation );
			COMPARECITYNAMEMASK( BridgePart );
			COMPARECITYNAMEMASK( GenericCityObject );
			COMPARECITYNAMEMASK( All );					
		}
#undef COMPARECITYNAMEMASK
		return mask;
	}

	void CityObject::finish( AppearanceManager& appearanceManager, bool optimize ) 
	{
		bool finish = false;

		Appearance* myappearance = appearanceManager.getAppearance( getId() );
		std::vector< Geometry* >::const_iterator it = _geometries.begin();
		for ( ; it != _geometries.end(); it++ ) (*it)->finish( appearanceManager, myappearance ? myappearance : NULL, optimize );

		while ( !finish && optimize ) 
		{
			finish = true;
			for ( int i = 0; finish && i < (int) _geometries.size() - 2; i++ ) 
			{
				for ( int j = i+1; finish && j < (int)_geometries.size() - 1; j++ ) 
				{
					if ( !_geometries[i]->merge( _geometries[j] ) ) continue;

					delete _geometries[j];
					
					//std::cout << " Obj : " << getId() << "... Geom " << _geometries[i]->getId() << " merged with " << _geometries[j]->getId() << std::endl;
					_geometries.erase( _geometries.begin() + j );
					finish = false;
				}
			}
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	CityModel::~CityModel( void ) 
	{ 	
		CityObjectsMap::const_iterator it = _cityObjectsMap.begin();
		for ( ; it != _cityObjectsMap.end(); it++ ) 
			for ( unsigned int i = 0; i < it->second.size(); i++ )
				delete it->second[i];
	}

	void CityModel::addCityObject( CityObject* o )
	{
		CityObjectsMap::iterator it = _cityObjectsMap.find( o->getType() );
		if ( it == _cityObjectsMap.end() )
		{
			CityObjects v;
			v.push_back( o );
			_cityObjectsMap[ o->getType() ] = v;
		}
		else
			it->second.push_back( o );
	}

	void CityModel::finish( bool optimize ) 
	{
		// Assign appearances to cityobjects => geometries => polygons
		CityObjectsMap::const_iterator it = _cityObjectsMap.begin();
		for ( ; it != _cityObjectsMap.end(); it++ ) 
			for ( unsigned int i = 0; i < it->second.size(); i++ )
				it->second[i]->finish( _appearanceManager, optimize );

		_appearanceManager.finish();
		
		Tesselator::destroy();
	}

	//////////////////////////////////////////////////////////////////////////////

	Tesselator* Tesselator::_instance = NULL;

	Tesselator::Tesselator( void )
	{
		_tobj = gluNewTess(); 

		gluTessCallback( _tobj, GLU_TESS_VERTEX_DATA, (GLU_TESS_CALLBACK)&vertexCallback );
		gluTessCallback( _tobj, GLU_TESS_BEGIN_DATA, (GLU_TESS_CALLBACK)&beginCallback );
		gluTessCallback( _tobj, GLU_TESS_END_DATA, (GLU_TESS_CALLBACK)&endCallback );
		gluTessCallback( _tobj, GLU_TESS_COMBINE_DATA, (GLU_TESS_CALLBACK)&combineCallback );
		gluTessCallback( _tobj, GLU_TESS_ERROR_DATA, (GLU_TESS_CALLBACK)&errorCallback );
	}

	void Tesselator::init( unsigned int verticesCount, const TVec3d& normal, GLenum winding_rule )
	{
		gluTessBeginPolygon( _tobj, this ); 

		gluTessProperty( _tobj, GLU_TESS_WINDING_RULE, winding_rule );
		gluTessNormal( _tobj, normal.x, normal.y, normal.z );
				
		_vertices.clear();
		_vertices.reserve( verticesCount );
		_indices.clear();
		_curIndices.clear();
	}

	Tesselator::~Tesselator( void ) 
	{
		gluDeleteTess( _tobj );
	}

	void Tesselator::compute( void ) 
	{
		gluTessEndPolygon( _tobj );  
	}

	void Tesselator::addContour( const std::vector<TVec3d>& pts )
	{		
		unsigned int len = pts.size();
		if ( len < 3 ) return;

		unsigned int pos = _vertices.size();
		
		gluTessBeginContour( _tobj );

		for ( unsigned int i = 0; i < len; i++ ) 
		{
			_vertices.push_back( pts[i] );

			gluTessVertex( _tobj, &(_vertices[pos+i][0]), (void*)(pos+i) );
		}

		gluTessEndContour( _tobj );
	}

	void CALLBACK Tesselator::beginCallback( GLenum which, void* userData ) 
	{
		Tesselator *tess = (Tesselator*)userData;
		tess->_curMode = which;
	}

	void CALLBACK Tesselator::vertexCallback( GLvoid *data, void* userData ) 
	{
		Tesselator *tess = (Tesselator*)userData;
		tess->_curIndices.push_back( (int)data );
	}

	void CALLBACK Tesselator::combineCallback( GLdouble coords[3], void* vertex_data[4], GLfloat weight[4], void** outData, void* userData )
	{
		Tesselator *tess = (Tesselator*)userData;
		unsigned int npoint = tess->_vertices.size();
		tess->_vertices.push_back( TVec3d( coords[0], coords[1], coords[2] ) );
		*outData = (void*)npoint;
	}

	void CALLBACK Tesselator::endCallback( void* userData ) 
	{
		Tesselator *tess = (Tesselator*)userData;

		unsigned int len = tess->_curIndices.size();

		switch ( tess->_curMode ) 
		{
		case GL_TRIANGLES:
			{
				for ( unsigned int i = 0; i < len; i++ ) 
					tess->_indices.push_back( tess->_curIndices[i] );
			}
			break;
		case GL_TRIANGLE_FAN:
		case GL_TRIANGLE_STRIP: 
			{
				unsigned int first = tess->_curIndices[0];
				unsigned int prev = tess->_curIndices[1];

				for ( unsigned int i = 2; i < len; i++ ) 
				{
					if ( tess->_curMode == GL_TRIANGLE_FAN || i%2 == 0 ) tess->_indices.push_back( first );
					tess->_indices.push_back( prev );
					if ( tess->_curMode == GL_TRIANGLE_STRIP )
					{
						if ( i%2 == 1) tess->_indices.push_back( first );
						first = prev;
					}
					prev = tess->_curIndices[i];
					tess->_indices.push_back( prev );
				}
			}
			break;
		default: std::cerr << "CityGML tesselator: non-supported GLU tesselator primitive " << tess->_curMode << std::endl;
		}
		tess->_curIndices.clear();
	}

	void CALLBACK Tesselator::errorCallback( GLenum errorCode, void* userData )
	{
		std::cerr << "CityGML tesselator error: " << gluErrorString( errorCode ) << std::endl;
	}
}
