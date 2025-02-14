////////////////////////////////////////////////////////////////////////////////////////////////////
/// \file   Leddar/LdLjrRecordReader.cpp
///
/// \brief  Implements the LdLjrRecordReader class
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "LdLjrRecordReader.h"

#include "LdLjrDefines.h"
#include "LdPropertyIds.h"
#include "LtStringUtils.h"

#include "LdDeviceFactory.h"
#include "LdSensorM16.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <cerrno>

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn LeddarRecord::LdLjrRecordReader::LdLjrRecordReader( const std::string &aFile )
///
/// \brief  Constructor
///
/// \exception  std::logic_error    Raised when the file could not be opened or the record is invalid.
/// \exception  std::runtime_error  Raised when a the header is missing / invalid. (from ReadHeader)
///
/// \param  aFile   The record file to open.
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
LeddarRecord::LdLjrRecordReader::LdLjrRecordReader( const std::string &aFile )
    : LdRecordReader()
    , mFile()
{
    mFile.open( aFile, std::ios_base::in );

    if( !mFile.is_open() )
    {
        throw std::logic_error( "Could not open file - Error code: " + LeddarUtils::LtStringUtils::IntToString( errno ) );
    }

    std::string lLine;
    uint32_t lNbLine = 0;

    while( std::getline( mFile, lLine ) )
        ++lNbLine;

    if( lNbLine < LJR_HEADER_LINES )
    {
        throw std::logic_error( "Record is too short." );
    }

    SetRecordSize( lNbLine - LJR_HEADER_LINES );

    mFile.clear();
    mFile.seekg( 0, std::ifstream::beg );

    std::getline( mFile, lLine );
    ++mCurrentLine;
    ReadHeader( lLine );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn LeddarRecord::LdLjrRecordReader::~LdLjrRecordReader()
///
/// \brief  Destructor
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
LeddarRecord::LdLjrRecordReader::~LdLjrRecordReader()
{
    if( !mFile.is_open() )
    {
        mFile.close();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn uint32_t LeddarRecord::LdLjrRecordReader::DeviceTypeFromProperties( const std::string &aFile )
///
/// \brief  Read the properties and get the device type
///
/// \param  aFile   The recording.
///
/// \returns    The device type.
///
/// \author David L�vy
/// \date   March 2021
////////////////////////////////////////////////////////////////////////////////////////////////////
uint32_t LeddarRecord::LdLjrRecordReader::DeviceTypeFromHeader( const std::string &aFile )
{
    std::ifstream lFile;
    lFile.open( aFile, std::ios_base::in );

    if( !lFile.is_open() )
    {
        throw std::logic_error( "Could not open file - Error code: " + LeddarUtils::LtStringUtils::IntToString( errno ) );
    }

    std::string lLine;

    if( !std::getline( lFile, lLine ) )
        throw std::runtime_error( "Cannot read first line of the file" );

    rapidjson::Document lDOM;
    lDOM.Parse( lLine.c_str() );

    if( lDOM.HasParseError() )
    {
        throw std::runtime_error( "Error parsing header" + std::string( rapidjson::GetParseError_En( lDOM.GetParseError() ) ) );
    }

    if( !lDOM.HasMember( "header" ) )
    {
        throw std::runtime_error( "First line of the file is not the header" );
    }

    if( lDOM["header"]["prot_version"].GetUint() != LeddarRecord::LJR_PROT_VERSION )
    {
        throw std::runtime_error( "Invalid ljr protocol version" );
    }

    return lDOM["header"]["devicetype"].GetUint();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn void LeddarRecord::LdLjrRecordReader::ReadNext()
///
/// \brief  Reads the next frame
///
/// \exception  std::out_of_range   Thrown when an end of file is reached.
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
void LeddarRecord::LdLjrRecordReader::ReadNext()
{
    std::string lLine;

    if( !std::getline( mFile, lLine ) )
    {
        throw std::out_of_range( "End of file reached" );
    }

    ++mCurrentLine;

    // Check first characters if its a frame or a property update
    if( lLine.compare( 2, 5, "frame" ) != 0 )
    {
        ReadProperties( lLine, PC_Sensor );
        ReadNext();
        return;
    }

    ReadFrame( lLine );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn void LeddarRecord::LdLjrRecordReader::ReadPrevious()
///
/// \brief  Reads the previous frame
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
void LeddarRecord::LdLjrRecordReader::ReadPrevious() { MoveTo( mCurrentLine - 1 - LJR_HEADER_LINES ); }

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn void LeddarRecord::LdLjrRecordReader::MoveTo( uint32_t aFrame )
///
/// \brief  Move to the specified frame
///
/// \exception  std::out_of_range   Thrown when the requested frame is out of range.
///
/// \param  aFrame  The frame to move to.
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
void LeddarRecord::LdLjrRecordReader::MoveTo( uint32_t aFrame )
{
    if( aFrame > GetRecordSize() )
    {
        throw std::out_of_range( "Requested frame larger than record size" );
    }

    // Not the fastest way, but the easiest to do.
    mFile.clear();
    mFile.seekg( 0, std::ifstream::beg );
    mCurrentLine = 0;

    for( uint32_t i = 0; i < aFrame + LJR_HEADER_LINES - 1; ++i )
    {
        std::string lLine;
        std::getline( mFile, lLine );
        ++mCurrentLine;
    }

    ReadNext();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn LeddarDevice::LdSensor *LeddarRecord::LdLjrRecordReader::CreateSensor()
///
/// \brief  Instantiate the sensor from his device type, and populate its properties from the record.
///
/// \exception  std::runtime_error  Raised when there is an error parsing properties. (from InitProperties / ReadProperties)
/// \exception  std::logic_error    Raised when there is an unsupported property type. (from InitProperties / ReadProperties)
///
/// \return nullptr if it fails, else the new sensor.
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
LeddarDevice::LdSensor *LeddarRecord::LdLjrRecordReader::CreateSensor()
{
    // Create the sensor from the device type
    mSensor = LeddarDevice::LdDeviceFactory::CreateSensorForRecording( GetDeviceType(), GetCommProtocol() );
    InitProperties();
    ReadNext();
    return mSensor;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn uint32_t LeddarRecord::LdLjrRecordReader::GetCurrentPosition() const
///
/// \brief  Gets current frame
///
/// \returns    The current frame.
///
/// \author David L�vy
/// \date   November 2020
////////////////////////////////////////////////////////////////////////////////////////////////////
uint32_t LeddarRecord::LdLjrRecordReader::GetCurrentPosition() const { return mCurrentLine - LJR_HEADER_LINES; }

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn void LeddarRecord::LdLjrRecordReader::InitProperties()
///
/// \brief  Initialize the properties that may be missing from recording
///
/// \exception  std::runtime_error  Raised when there is an error parsing properties. (from ReadProperties)
/// \exception  std::logic_error    Raised when there is an unsupported property type. (from ReadProperties)
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
void LeddarRecord::LdLjrRecordReader::InitProperties()
{
    std::string lLine;
    ++mCurrentLine;
    std::getline( mFile, lLine ); // Line2

    ReadProperties( lLine, PC_Sensor ); // Read all properties
    mSensor->UpdateConstants();         // Update the scale
    ReadProperties( lLine, PC_Sensor ); // Re-read the properties so they have the correct values with the scale

    // Init states/echoes/traces buffers
    uint16_t lVSegments = 1, lHSegments = 1;
    uint16_t lRefSeg = 0;

    if( mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_VSEGMENT ) &&
        mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_VSEGMENT )->Count() > 0 )
    {
        lVSegments = mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_VSEGMENT )->ValueT<uint16_t>();
    }

    if( mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_RSEGMENT ) != nullptr &&
        mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_RSEGMENT )->Count() > 0 )
    {
        lRefSeg = mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_RSEGMENT )->ValueT<uint16_t>();
    }

    if( mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_HSEGMENT )->Count() > 0 )
    {
        lHSegments = mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_HSEGMENT )->ValueT<uint16_t>();
    }

    uint32_t lTotalSegments  = lVSegments * lHSegments + lRefSeg;
    uint32_t lMaxTotalEchoes = 0;

    if( mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_MAX_ECHOES_PER_CHANNEL ) )
    {
        lMaxTotalEchoes = lTotalSegments * mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_MAX_ECHOES_PER_CHANNEL )->ValueT<uint8_t>();
    }
    else
    {
        lMaxTotalEchoes = lTotalSegments * 8;
    }

    uint32_t lDistScale = 1, lAmpScale = 1;

    if( mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_DISTANCE_SCALE ) )
        lDistScale = mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_DISTANCE_SCALE )->ValueT<uint32_t>();

    if( mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_FILTERED_AMP_SCALE ) )
        lAmpScale = mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_FILTERED_AMP_SCALE )->ValueT<uint32_t>();

    mSensor->GetResultEchoes()->Init( lDistScale, lAmpScale, lMaxTotalEchoes );
    mSensor->GetResultEchoes()->Swap();

    uint32_t lCpuLoadScale = 0, lTemperatureScale = 0;

    if( mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_CPU_LOAD_SCALE ) &&
        mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_CPU_LOAD_SCALE )->Count() > 0 )
    {
        lCpuLoadScale = mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_CPU_LOAD_SCALE )->ValueT<uint32_t>();
    }

    if( mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_TEMPERATURE_SCALE ) &&
        mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_TEMPERATURE_SCALE )->Count() > 0 )
    {
        lTemperatureScale = mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_TEMPERATURE_SCALE )->ValueT<uint32_t>();
    }

#if defined( BUILD_M16 ) && defined( BUILD_USB )
    else if( dynamic_cast<LeddarDevice::LdSensorM16 *>( mSensor ) && mSensor->GetProperties()->FindProperty( LeddarCore::LdPropertyIds::ID_DISTANCE_SCALE ) )
    {
        lTemperatureScale = mSensor->GetProperties()->GetIntegerProperty( LeddarCore::LdPropertyIds::ID_DISTANCE_SCALE )->ValueT<uint32_t>();
    }

#endif
    mSensor->GetResultStates()->Init( lTemperatureScale, lCpuLoadScale );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn void LeddarRecord::LdLjrRecordReader::ReadHeader( const std::string &aLine )
///
/// \brief  Reads the header of the record file
///
/// \exception  std::runtime_error  Raised when a the header is missing / invalid.
///
/// \param  aLine   The line.
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
void LeddarRecord::LdLjrRecordReader::ReadHeader( const std::string &aLine )
{
    rapidjson::Document lDOM;
    lDOM.Parse( aLine.c_str() );

    if( lDOM.HasParseError() )
    {
        throw std::runtime_error( "Error parsing header" + std::string( rapidjson::GetParseError_En( lDOM.GetParseError() ) ) );
    }

    if( !lDOM.HasMember( "header" ) )
    {
        throw std::runtime_error( "First line of the file is not the header" );
    }

    if( lDOM["header"]["prot_version"].GetUint() != LeddarRecord::LJR_PROT_VERSION )
    {
        throw std::runtime_error( "Invalid ljr protocol version" );
    }

    SetDeviceType( lDOM["header"]["devicetype"].GetUint() );
    SetCommProtocol( static_cast<LeddarDevice::LdSensor::eProtocol>( lDOM["header"]["protocol"].GetUint() ) );
    SetRecordTimestamp( lDOM["header"]["timestamp"].GetUint64() );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn void LeddarRecord::LdLjrRecordReader::ReadProperties( const std::string &aLine )
///
/// \brief  Reads the properties from the record
///
/// \exception  std::runtime_error  Raised when there is an error parsing properties.
/// \exception  std::logic_error    Raised when there is an unsupported property type.
///
/// \param  aLine   The line.
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
void LeddarRecord::LdLjrRecordReader::ReadProperties( const std::string &aLine, ePropContainer aContainer )
{
    rapidjson::Document lDOM;
    lDOM.Parse( aLine.c_str() );

    if( lDOM.HasParseError() )
    {
        throw std::runtime_error( "Error parsing properties" + std::string( rapidjson::GetParseError_En( lDOM.GetParseError() ) ) );
    }

    LeddarCore::LdPropertiesContainer *lProperties                                    = nullptr;
    const rapidjson::GenericArray<false, rapidjson::Value::ValueType> *lPropArrayInit = nullptr;

    if( aContainer == PC_States )
    {
        if( !lDOM["frame"].HasMember( "states" ) || !lDOM["frame"]["states"].IsArray() )
        {
            throw std::runtime_error( "Could not read states properties." );
        }

        lProperties    = mSensor->GetResultStates()->GetProperties();
        lPropArrayInit = new const rapidjson::GenericArray<false, rapidjson::Value::ValueType>( lDOM["frame"]["states"].GetArray() );
    }
    else if( aContainer == PC_Echoes )
    {
        if( !lDOM["frame"].HasMember( "echoes_prop" ) || !lDOM["frame"]["echoes_prop"].IsArray() )
        {
            throw std::runtime_error( "Could not read echoes properties." );
        }

        ReadEchoProperties( aLine );
        return;
    }
    else if( aContainer == PC_Sensor )
    {
        if( !lDOM.HasMember( "prop" ) || !lDOM["prop"].IsArray() )
        {
            throw std::runtime_error( "Record line is not a properties line." );
        }

        lProperties    = mSensor->GetProperties();
        lPropArrayInit = new const rapidjson::GenericArray<false, rapidjson::Value::ValueType>( lDOM["prop"].GetArray() );
    }
    else
    {
        throw std::invalid_argument( "Invalid property container" );
    }

    const rapidjson::GenericArray<false, rapidjson::Value::ValueType> &lPropArray = *lPropArrayInit;

    for( unsigned i = 0; i < lPropArray.Size(); ++i )
    {
        if( !lPropArray[i].HasMember( "id" ) || !lPropArray[i].HasMember( "val" ) )
            continue;

        if( auto *lProp = lProperties->FindProperty( lPropArray[i]["id"].GetUint() ) )
        {
            switch( lProp->GetType() )
            {
            case LeddarCore::LdProperty::TYPE_BITFIELD:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lProp->SetCount( lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        dynamic_cast<LeddarCore::LdBitFieldProperty *>( lProp )->ForceValue( j, lPropArray[i]["val"].GetArray()[j].GetUint64() );
                    }
                }
                else
                {
                    lProp->SetCount( 1 );
                    dynamic_cast<LeddarCore::LdBitFieldProperty *>( lProp )->ForceValue( 0, lPropArray[i]["val"].GetUint64() );
                }

                lProp->SetClean();
                break;

            case LeddarCore::LdProperty::TYPE_BOOL:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lProp->SetCount( lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        dynamic_cast<LeddarCore::LdBoolProperty *>( lProp )->ForceValue( j, lPropArray[i]["val"].GetArray()[j].GetBool() );
                    }
                }
                else
                {
                    lProp->SetCount( 1 );
                    dynamic_cast<LeddarCore::LdBoolProperty *>( lProp )->ForceValue( 0, lPropArray[i]["val"].GetBool() );
                }

                lProp->SetClean();
                break;

            case LeddarCore::LdProperty::TYPE_ENUM:
            {
                if( lPropArray[i].HasMember( "enum" ) ) // Should be here only on the full properties line, not when a value is updated
                {
                    rapidjson::GenericObject<false, rapidjson::Value::ValueType> lEnumValues = lPropArray[i]["enum"].GetObject();

                    for( rapidjson::Value::ConstMemberIterator itr = lEnumValues.MemberBegin(); itr != lEnumValues.MemberEnd(); ++itr )
                    {
                        dynamic_cast<LeddarCore::LdEnumProperty *>( lProp )->AddEnumPair( itr->value.GetUint64(), itr->name.GetString() );
                    }
                }

                if( lPropArray[i]["val"].IsArray() )
                {
                    lProp->SetCount( lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        dynamic_cast<LeddarCore::LdEnumProperty *>( lProp )->ForceValue( j, lPropArray[i]["val"].GetArray()[j].GetUint64() );
                    }
                }
                else
                {
                    lProp->SetCount( 1 );
                    dynamic_cast<LeddarCore::LdEnumProperty *>( lProp )->ForceValue( 0, lPropArray[i]["val"].GetUint64() );
                }
            }

                lProp->SetClean();
                break;

            case LeddarCore::LdProperty::TYPE_FLOAT:
                if( lPropArray[i].HasMember( "limits" ) )
                    dynamic_cast<LeddarCore::LdFloatProperty *>( lProp )->SetLimits( static_cast<float>( lPropArray[i]["limits"][0].GetDouble() ),
                                                                                     static_cast<float>( lPropArray[i]["limits"][1].GetDouble() ) );

                if( lPropArray[i]["val"].IsArray() )
                {
                    lProp->SetCount( lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        dynamic_cast<LeddarCore::LdFloatProperty *>( lProp )->ForceValue( j, lPropArray[i]["val"].GetArray()[j].GetFloat() );
                    }
                }
                else
                {
                    lProp->SetCount( 1 );
                    dynamic_cast<LeddarCore::LdFloatProperty *>( lProp )->ForceValue( 0, lPropArray[i]["val"].GetFloat() );
                }

                lProp->SetClean();
                break;

            case LeddarCore::LdProperty::TYPE_INTEGER:
            {
                bool lSigned = lProp->Signed();

                if( lPropArray[i].HasMember( "signed" ) && lSigned != lPropArray[i]["signed"].GetBool() )
                {
                    throw std::logic_error( "Signed / unsigned property mismatch" );
                }

                if( lSigned )
                {
                    if( lPropArray[i].HasMember( "limits" ) )
                    {
                        dynamic_cast<LeddarCore::LdIntegerProperty *>( lProp )->SetLimits( lPropArray[i]["limits"][0].GetInt64(), lPropArray[i]["limits"][1].GetInt64() );
                    }

                    if( lPropArray[i]["val"].IsArray() )
                    {
                        lProp->SetCount( lPropArray[i]["val"].GetArray().Size() );

                        for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                        {
                            dynamic_cast<LeddarCore::LdIntegerProperty *>( lProp )->ForceValue( j, lPropArray[i]["val"].GetArray()[j].GetInt64() );
                        }
                    }
                    else
                    {
                        lProp->SetCount( 1 );
                        dynamic_cast<LeddarCore::LdIntegerProperty *>( lProp )->ForceValue( 0, lPropArray[i]["val"].GetInt64() );
                    }
                }
                else
                {
                    if( lPropArray[i].HasMember( "limits" ) )
                    {
                        dynamic_cast<LeddarCore::LdIntegerProperty *>( lProp )->SetLimitsUnsigned( lPropArray[i]["limits"][0].GetUint64(), lPropArray[i]["limits"][1].GetUint64() );
                    }

                    if( lPropArray[i]["val"].IsArray() )
                    {
                        lProp->SetCount( lPropArray[i]["val"].GetArray().Size() );

                        for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                        {
                            dynamic_cast<LeddarCore::LdIntegerProperty *>( lProp )->ForceValueUnsigned( j, lPropArray[i]["val"].GetArray()[j].GetUint64() );
                        }
                    }
                    else
                    {
                        lProp->SetCount( 1 );
                        dynamic_cast<LeddarCore::LdIntegerProperty *>( lProp )->ForceValueUnsigned( 0, lPropArray[i]["val"].GetUint64() );
                    }
                }

                lProp->SetClean();
            }
            break;

            case LeddarCore::LdProperty::TYPE_TEXT:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lProp->SetCount( lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        dynamic_cast<LeddarCore::LdTextProperty *>( lProp )->ForceValue( j, lPropArray[i]["val"].GetArray()[j].GetString() );
                    }
                }
                else
                {
                    lProp->SetCount( 1 );
                    dynamic_cast<LeddarCore::LdTextProperty *>( lProp )->ForceValue( 0, lPropArray[i]["val"].GetString() );
                }

                lProp->SetClean();
                break;

            case LeddarCore::LdProperty::TYPE_BUFFER:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lProp->SetCount( lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        lProp->ForceStringValue( j, lPropArray[i]["val"].GetArray()[j].GetString() );
                    }
                }
                else
                {
                    lProp->SetCount( 1 );
                    lProp->ForceStringValue( 0, lPropArray[i]["val"].GetString() );
                }

                lProp->SetClean();
                break;

            default:
                throw std::logic_error( "Unsupported property type" );
            }
        }
    }

    delete lPropArrayInit;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn void LeddarRecord::LdLjrRecordReader::ReadEchoProperties( const std::string &aLine )
///
/// \brief  Reads echo properties
///
/// \param  aLine   The line.
///
/// \author David L�vy
/// \date   March 2021
////////////////////////////////////////////////////////////////////////////////////////////////////
void LeddarRecord::LdLjrRecordReader::ReadEchoProperties( const std::string &aLine )
{
    rapidjson::Document lDOM;
    lDOM.Parse( aLine.c_str() );

    if( lDOM.HasParseError() )
    {
        throw std::runtime_error( "Error parsing properties" + std::string( rapidjson::GetParseError_En( lDOM.GetParseError() ) ) );
    }

    auto lPropArray     = lDOM["frame"]["echoes_prop"].GetArray();
    auto *lResultEchoes = mSensor->GetResultEchoes();

    for( unsigned i = 0; i < lPropArray.Size(); ++i )
    {
        if( !lPropArray[i].HasMember( "id" ) || !lPropArray[i].HasMember( "val" ) )
            continue;

        if( auto *lProp = lResultEchoes->GetProperties()->FindProperty( lPropArray[i]["id"].GetUint() ) )
        {
            switch( lProp->GetType() )
            {
            case LeddarCore::LdProperty::TYPE_BITFIELD:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), lPropArray[i]["val"].GetArray().Size() );
                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        lResultEchoes->SetPropertyValue( lProp->GetId(), j, lPropArray[i]["val"].GetArray()[j].GetUint64() );
                    }
                }
                else
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), 1 );
                    lResultEchoes->SetPropertyValue( lProp->GetId(), 0, lPropArray[i]["val"].GetUint64() );
                }
                break;

            case LeddarCore::LdProperty::TYPE_ENUM:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), lPropArray[i]["val"].GetArray().Size() );
                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        lResultEchoes->SetPropertyValue( lProp->GetId(), j, lPropArray[i]["val"].GetArray()[j].GetUint64() );
                    }
                }
                else
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), 1 );
                    lResultEchoes->SetPropertyValue( lProp->GetId(), 0, lPropArray[i]["val"].GetUint64() );
                }
                break;

            case LeddarCore::LdProperty::TYPE_BOOL:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        lResultEchoes->SetPropertyValue( lProp->GetId(), j, lPropArray[i]["val"].GetArray()[j].GetBool() );
                    }
                }
                else
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), 1 );
                    lResultEchoes->SetPropertyValue( lProp->GetId(), 0, lPropArray[i]["val"].GetBool() );
                }
                break;

            case LeddarCore::LdProperty::TYPE_FLOAT:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        lResultEchoes->SetPropertyValue( lProp->GetId(), j, lPropArray[i]["val"].GetArray()[j].GetFloat() );
                    }
                }
                else
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), 1 );
                    lResultEchoes->SetPropertyValue( lProp->GetId(), 0, lPropArray[i]["val"].GetFloat() );
                }

                break;

            case LeddarCore::LdProperty::TYPE_INTEGER:
            {
                bool lSigned = lProp->Signed();

                if( lPropArray[i].HasMember( "signed" ) && lSigned != lPropArray[i]["signed"].GetBool() )
                {
                    throw std::logic_error( "Signed / unsigned property mismatch" );
                }

                if( lSigned )
                {
                    if( lPropArray[i]["val"].IsArray() )
                    {
                        lResultEchoes->SetPropertyCount( lProp->GetId(), lPropArray[i]["val"].GetArray().Size() );

                        for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                        {
                            lResultEchoes->SetPropertyValue( lProp->GetId(), j, lPropArray[i]["val"].GetArray()[j].GetInt64() );
                        }
                    }
                    else
                    {
                        lResultEchoes->SetPropertyCount( lProp->GetId(), 1 );
                        lResultEchoes->SetPropertyValue( lProp->GetId(), 0, lPropArray[i]["val"].GetInt64() );
                    }
                }
                else
                {
                    if( lPropArray[i]["val"].IsArray() )
                    {
                        lResultEchoes->SetPropertyCount( lProp->GetId(), lPropArray[i]["val"].GetArray().Size() );

                        for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                        {
                            lResultEchoes->SetPropertyValue( lProp->GetId(), j, lPropArray[i]["val"].GetArray()[j].GetUint64() );
                        }
                    }
                    else
                    {
                        lResultEchoes->SetPropertyCount( lProp->GetId(), 1 );
                        lResultEchoes->SetPropertyValue( lProp->GetId(), 0, lPropArray[i]["val"].GetUint64() );
                    }
                }
            }
            break;

            case LeddarCore::LdProperty::TYPE_TEXT:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        lResultEchoes->SetPropertyValue( lProp->GetId(), j, lPropArray[i]["val"].GetArray()[j].GetString() );
                    }
                }
                else
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), 1 );
                    lResultEchoes->SetPropertyValue( lProp->GetId(), 0, lPropArray[i]["val"].GetString() );
                }

                break;

            case LeddarCore::LdProperty::TYPE_BUFFER:
                if( lPropArray[i]["val"].IsArray() )
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), lPropArray[i]["val"].GetArray().Size() );

                    for( unsigned j = 0; j < lPropArray[i]["val"].GetArray().Size(); ++j )
                    {
                        lResultEchoes->SetPropertyValue( lProp->GetId(), j, lPropArray[i]["val"].GetArray()[j].GetString() );
                    }
                }
                else
                {
                    lResultEchoes->SetPropertyCount( lProp->GetId(), 1 );
                    lResultEchoes->SetPropertyValue( lProp->GetId(), 0, lPropArray[i]["val"].GetString() );
                }

                break;

            default:
                throw std::logic_error( "Unsupported property type" );
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \fn void LeddarRecord::LdLjrRecordReader::ReadFrame( const std::string &aLine )
///
/// \brief  Reads a frame from the line
///
/// \param  aLine   The line.
///
/// \author David Levy
/// \date   October 2018
////////////////////////////////////////////////////////////////////////////////////////////////////
void LeddarRecord::LdLjrRecordReader::ReadFrame( const std::string &aLine )
{
    rapidjson::Document lDOM;
    lDOM.Parse( aLine.c_str() );

    uint32_t lTimestamp = 0;

    if( lDOM.HasMember( "frame" ) && lDOM["frame"].HasMember( "ts" ) ) //For retro compatiblity - Field does not exist anymore
    {
        lTimestamp = lDOM["frame"]["ts"].GetUint();
    }

    if( lDOM.HasParseError() )
    {
        throw std::runtime_error( "Error parsing frame" + std::string( rapidjson::GetParseError_En( lDOM.GetParseError() ) ) );
    }

    if( !lDOM.HasMember( "frame" ) || !lDOM["frame"].IsObject() )
    {
        throw std::runtime_error( "Record line is not a frame." );
    }

    if( lDOM["frame"].HasMember( "states" ) )
    {
        if(lTimestamp != 0)
            mSensor->GetResultStates()->SetTimestamp(lTimestamp);

        ReadProperties( aLine, PC_States );
    }

    if( lDOM["frame"].HasMember( "echoes_prop" ) )
    {
        ReadProperties( aLine, PC_Echoes );
    }

    if( lDOM["frame"].HasMember( "echoes" ) )
    {
        if(lTimestamp != 0)
            mSensor->GetResultEchoes()->SetTimestamp(lTimestamp);

        rapidjson::GenericArray<false, rapidjson::Value::ValueType> lEchoesArray = lDOM["frame"]["echoes"].GetArray();

        auto *lResultEchoes = mSensor->GetResultEchoes();
        auto lLock          = lResultEchoes->GetUniqueLock( LeddarConnection::B_SET );
        lResultEchoes->SetEchoCount( lEchoesArray.Size() );

        std::vector<LeddarConnection::LdEcho> &lEchoes = *( lResultEchoes->GetEchoes( LeddarConnection::B_SET ) );
        uint32_t lDistanceScale                        = lResultEchoes->GetDistanceScale();
        uint32_t lAmplitudeScale                       = lResultEchoes->GetAmplitudeScale();

        for( unsigned i = 0; i < lEchoesArray.Size(); ++i )
        {
            LeddarConnection::LdEcho lEcho = {};
            lEcho.mChannelIndex            = lEchoesArray[i][0].GetUint();
            lEcho.mDistance                = static_cast<int32_t>( lEchoesArray[i][1].GetDouble() * lDistanceScale );
            lEcho.mAmplitude               = static_cast<uint32_t>( lEchoesArray[i][2].GetDouble() * lAmplitudeScale );
            lEcho.mFlag                    = static_cast<uint16_t>( lEchoesArray[i][3].GetUint() );
            lEcho.mX                       = static_cast<float>( lEchoesArray[i][4].GetDouble() );
            lEcho.mY                       = static_cast<float>( lEchoesArray[i][5].GetDouble() );
            lEcho.mZ                       = static_cast<float>( lEchoesArray[i][6].GetDouble() );
            lEcho.mTimestamp               = lEchoesArray[i][7].GetUint64();
            lEchoes[i]                     = lEcho;
        }

        lLock.unlock();
        mSensor->ComputeCartesianCoordinates();
        lResultEchoes->Swap();
        lResultEchoes->UpdateFinished();
    }
}
