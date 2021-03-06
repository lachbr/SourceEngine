#include "VertexBufferDx11.h"
#include "shaderapidx9/locald3dtypes.h"
#include "shaderapidx11_global.h"
#include "shaderdevicedx11.h"
#include "shaderapi/ishaderutil.h"
#include "tier0/vprof.h"
#include "tier0/dbg.h"
#include "materialsystem/ivballoctracker.h"
#include "tier2/tier2.h"

// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
//
// Dx11 implementation of a vertex buffer
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// globals
//-----------------------------------------------------------------------------
#ifdef _DEBUG
int CVertexBufferDx11::s_nBufferCount = 0;
#endif


//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
CVertexBufferDx11::CVertexBufferDx11( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char* pBudgetGroupName ) :
	BaseClass( pBudgetGroupName )
{
	Assert( nVertexCount != 0 );

	m_pVertexBuffer = NULL;
	m_VertexFormat = fmt;
	m_nVertexCount = ( fmt == VERTEX_FORMAT_UNKNOWN ) ? 0 : nVertexCount;
	m_VertexSize = VertexFormatSize( m_VertexFormat );
	m_nBufferSize = ( fmt == VERTEX_FORMAT_UNKNOWN ) ? nVertexCount : nVertexCount * VertexSize();
	m_nFirstUnwrittenOffset = 0;
	m_bIsLocked = false;
	m_bIsDynamic = ( type == SHADER_BUFFER_TYPE_DYNAMIC ) || ( type == SHADER_BUFFER_TYPE_DYNAMIC_TEMP );
	m_bFlush = false;
}

CVertexBufferDx11::~CVertexBufferDx11()
{
	Free();
}

bool CVertexBufferDx11::HasEnoughRoom( int nVertCount ) const
{
	return ( GetRoomRemaining() - nVertCount ) >= 0;
}


//-----------------------------------------------------------------------------
// Creates, destroys the vertex buffer
//-----------------------------------------------------------------------------
bool CVertexBufferDx11::Allocate()
{
	Assert( !m_pVertexBuffer );

	m_nFirstUnwrittenOffset = 0;

	D3D11_BUFFER_DESC bd;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = m_nBufferSize;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;

	Log( "Creating D3D vertex buffer: size: %i\n", m_nBufferSize );

	//if ( m_nBufferSize <= 0 )
	//{
	//	int *p = 0;
	//	*p = 1;
	//}
		

	HRESULT hr = D3D11Device()->CreateBuffer( &bd, NULL, &m_pVertexBuffer );
	bool bOk = !FAILED( hr ) && ( m_pVertexBuffer != 0 );

	if ( bOk )
	{
		// Track VB allocations
		g_VBAllocTracker->CountVB( m_pVertexBuffer, m_bIsDynamic, m_nBufferSize, VertexSize(), GetVertexFormat() );

		if ( !m_bIsDynamic )
		{
			VPROF_INCREMENT_GROUP_COUNTER( "TexGroup_global_" TEXTURE_GROUP_STATIC_INDEX_BUFFER,
						       COUNTER_GROUP_TEXTURE_GLOBAL, m_nBufferSize );
		}
		else
		{
			VPROF_INCREMENT_GROUP_COUNTER( "TexGroup_global_" TEXTURE_GROUP_DYNAMIC_INDEX_BUFFER,
						       COUNTER_GROUP_TEXTURE_GLOBAL, m_nBufferSize );
					       // Dynamic meshes should never be compressed (slows down writing to them)
			Assert( CompressionType( GetVertexFormat() ) == VERTEX_COMPRESSION_NONE );
		}
#ifdef _DEBUG
		++s_nBufferCount;
#endif
	}

	return bOk;
}

void CVertexBufferDx11::Free()
{
	if ( m_pVertexBuffer )
	{
#ifdef _DEBUG
		--s_nBufferCount;
#endif

		// Track VB allocations
		g_VBAllocTracker->UnCountVB( m_pVertexBuffer );

		m_pVertexBuffer->Release();
		m_pVertexBuffer = NULL;

		if ( !m_bIsDynamic )
		{
			VPROF_INCREMENT_GROUP_COUNTER( "TexGroup_global_" TEXTURE_GROUP_STATIC_INDEX_BUFFER,
						       COUNTER_GROUP_TEXTURE_GLOBAL, -m_nBufferSize );
		}
		else
		{
			VPROF_INCREMENT_GROUP_COUNTER( "TexGroup_global_" TEXTURE_GROUP_DYNAMIC_INDEX_BUFFER,
						       COUNTER_GROUP_TEXTURE_GLOBAL, -m_nBufferSize );
		}
	}
}


//-----------------------------------------------------------------------------
// Vertex Buffer info
//-----------------------------------------------------------------------------
int CVertexBufferDx11::VertexCount() const
{
	//Assert( !m_bIsDynamic );
	return m_nVertexCount;
}


//-----------------------------------------------------------------------------
// Returns the buffer format (only valid for static index buffers)
//-----------------------------------------------------------------------------
VertexFormat_t CVertexBufferDx11::GetVertexFormat() const
{
	//Assert( !m_bIsDynamic );
	return m_VertexFormat;
}


//-----------------------------------------------------------------------------
// Returns true if the buffer is dynamic
//-----------------------------------------------------------------------------
bool CVertexBufferDx11::IsDynamic() const
{
	return m_bIsDynamic;
}


//-----------------------------------------------------------------------------
// Only used by dynamic buffers, indicates the next lock should perform a discard.
//-----------------------------------------------------------------------------
void CVertexBufferDx11::Flush()
{
	// This strange-looking line makes a flush only occur if the buffer is dynamic.
	m_bFlush = m_bIsDynamic;
}


//-----------------------------------------------------------------------------
// Casts a dynamic buffer to be a particular vertex type
//-----------------------------------------------------------------------------
void CVertexBufferDx11::BeginCastBuffer( VertexFormat_t format )
{
	Assert( format != VERTEX_FORMAT_UNKNOWN );
	Assert( m_bIsDynamic && ( m_VertexFormat == VERTEX_FORMAT_UNKNOWN || m_VertexFormat == format ) );
	if ( !m_bIsDynamic )
		return;

	m_VertexFormat = format;
	m_VertexSize = VertexFormatSize( m_VertexFormat );
	m_nVertexCount = m_nBufferSize / VertexSize();
}

void CVertexBufferDx11::EndCastBuffer()
{
	Assert( m_bIsDynamic && m_VertexFormat != 0 );
	if ( !m_bIsDynamic )
		return;
	m_VertexFormat = 0;
	m_nVertexCount = 0;
	m_VertexSize = 0;
}


//-----------------------------------------------------------------------------
// Returns the number of indices that can be written into the buffer
//-----------------------------------------------------------------------------
int CVertexBufferDx11::GetRoomRemaining() const
{
	return ( m_nBufferSize - m_nFirstUnwrittenOffset ) / VertexSize();
}


//-----------------------------------------------------------------------------
// Lock, unlock
//-----------------------------------------------------------------------------
bool CVertexBufferDx11::Lock( int nMaxVertexCount, bool bAppend, VertexDesc_t& desc )
{
	Assert( !m_bIsLocked && ( nMaxVertexCount != 0 ) && ( nMaxVertexCount <= m_nVertexCount ) );
	Assert( m_VertexFormat != 0 );

	// FIXME: Why do we need to sync matrices now?
	ShaderUtil()->SyncMatrices();
	g_ShaderMutex.Lock();

	D3D11_MAPPED_SUBRESOURCE lockedData;
	HRESULT hr;

	// This can happen if the buffer was locked but a type wasn't bound
	if ( m_VertexFormat == 0 )
	{
		Log( "No vertex format!\n" );
		goto vertexBufferLockFailed;
	}
		

	// Just give the app crap buffers to fill up while we're suppressed...
	if ( g_pShaderDevice->IsDeactivated() || ( nMaxVertexCount == 0 ) )
		goto vertexBufferLockFailed;

	// Did we ask for something too large?
	if ( nMaxVertexCount > m_nVertexCount )
	{
		Warning( "Too many vertices for vertex buffer. . tell a programmer (%d>%d)\n", nMaxVertexCount, m_nVertexCount );
		goto vertexBufferLockFailed;
	}

	// We might not have a buffer owing to alt-tab type stuff
	if ( !m_pVertexBuffer )
	{
		if ( !Allocate() )
		{
			Log( "Couldn't allocate vertex buffer\n" );
			goto vertexBufferLockFailed;
		}
			
	}

	Log( "Locking vertex buffer %p\n", m_pVertexBuffer );

	// Check to see if we have enough memory 
	int nMemoryRequired = nMaxVertexCount * VertexSize();
	bool bHasEnoughMemory = ( m_nFirstUnwrittenOffset + nMemoryRequired <= m_nBufferSize );

	D3D11_MAP map;
	if ( bAppend )
	{
		// Can't have the first lock after a flush be an appending lock
		Assert( !m_bFlush );

		// If we're appending and we don't have enough room, then puke!
		if ( !bHasEnoughMemory || m_bFlush )
			goto vertexBufferLockFailed;
		map = ( m_nFirstUnwrittenOffset == 0 ) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
	}
	else
	{
		// If we're not appending, no overwrite unless we don't have enough room
		// If we're a static buffer, always discard if we're not appending
		if ( !m_bFlush && bHasEnoughMemory && m_bIsDynamic )
		{
			map = ( m_nFirstUnwrittenOffset == 0 ) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
		}
		else
		{
			map = D3D11_MAP_WRITE_DISCARD;
			m_nFirstUnwrittenOffset = 0;
			m_bFlush = false;
		}
	}
	//goto vertexBufferLockFailed;
	hr = D3D11DeviceContext()->Map( m_pVertexBuffer, 0, map, 0, &lockedData );
	if ( FAILED( hr ) )
	{
		Warning( "Failed to lock vertex buffer in CVertexBufferDx11::Lock\n" );
		goto vertexBufferLockFailed;
	}

	ComputeVertexDescription( (unsigned char*)lockedData.pData + m_nFirstUnwrittenOffset, m_VertexFormat, desc );
	desc.m_nFirstVertex = m_nFirstUnwrittenOffset / VertexSize();
	desc.m_nOffset = m_nFirstUnwrittenOffset;
	m_bIsLocked = true;
	return true;

vertexBufferLockFailed:
	g_ShaderMutex.Unlock();

	// Set up a bogus index descriptor
	ComputeVertexDescription( 0, 0, desc );
	desc.m_nFirstVertex = 0;
	desc.m_nOffset = 0;
	return false;
}

void CVertexBufferDx11::Unlock( int nWrittenVertexCount, VertexDesc_t& desc )
{
	Log( "Unlocking vertex buffer %p\n", m_pVertexBuffer );
	Assert( nWrittenVertexCount <= m_nVertexCount );

	// NOTE: This can happen if the lock occurs during alt-tab
	// or if another application is initializing
	if ( !m_bIsLocked )
		return;

	if ( m_pVertexBuffer )
	{
		D3D11DeviceContext()->Unmap( m_pVertexBuffer, 0 );
	}

	Spew( nWrittenVertexCount, desc );

	m_nFirstUnwrittenOffset += nWrittenVertexCount * VertexSize();
	m_bIsLocked = false;
	g_ShaderMutex.Unlock();
}