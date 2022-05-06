#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __STDC_VERSION__ < 199901L
typedef unsigned short int uint16_t;
typedef unsigned int uint32_t;
#else
#include <stdint.h>
#endif

/* KOEI LS11 extractor */

typedef struct _track_t{
  int32_t length;
  int32_t extsiz;
  int32_t offset;
  struct _track_t *next;
} LS11_track_info_t;

#define LS11_DICTIONARY_SIZE 256

typedef struct{
  uint8_t dictionary[LS11_DICTIONARY_SIZE];
  LS11_track_info_t *track_info;
} LS11_header_t;

int LS11_read_and_check(FILE *fp)
{
  char header[16];
  char ls11_valid[] = { 'L', 'S', '1', '1', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  if( fread( header, sizeof(char), 16, fp ) != 16 || strcmp( header, ls11_valid ) != 0 ){
    fprintf( stderr, "not a LS11 file.\n" );
    return -1;
  }
  return 0;
}

int LS11_read_dictionary(FILE *fp, LS11_header_t *header)
{
  register int i;

  for( i=0; i<LS11_DICTIONARY_SIZE; i++ ){
    if( fread( &header->dictionary[i], sizeof(uint8_t), 1, fp ) < 1 ){
      fprintf( stderr, "invalid LS11 file.\n" );
      return -1;
    }
  }
  return 0;
}

int32_t LS11_read_int32(FILE *fp)
{
  union{
    uint8_t dat8[4];
    int32_t dat32;
  } dat;
  register int i;

  for( i=0; i<4; i++ )
    if( fread( &dat.dat8[3-i], sizeof(uint8_t), 1, fp ) < 1 ) return -1;
  return dat.dat32;
}

int LS11_read_track_info(FILE *fp, LS11_header_t *header)
{
  LS11_track_info_t *ti, *ti_last;
  int32_t val;

  for( header->track_info=ti_last=NULL; !feof( fp ); ){
    if( ( val = LS11_read_int32( fp ) ) == 0 ) break;
    if( !( ti = malloc( sizeof(LS11_track_info_t) ) ) ){
      fprintf( stderr, "cannot allocate memory.\n" );
      return -1;
    }
    ti->length = val;
    ti->extsiz = LS11_read_int32( fp );
    ti->offset = LS11_read_int32( fp );
    ti->next = NULL;
    if( !ti_last )
      header->track_info = ti;
    else
      ti_last->next = ti;
    ti_last = ti;
  }
  return 0;
}

int LS11_read_header(FILE *fp, LS11_header_t *header)
{
  if( LS11_read_and_check( fp ) < 0 ||
      LS11_read_dictionary( fp, header ) < 0 ||
      LS11_read_track_info( fp, header ) )
    return -1;
  return 0;
}

/* for debug */
void LS11_header_write(LS11_header_t *header)
{
  LS11_track_info_t *ti;
  int count;

  for( count=0, ti=header->track_info; ti; ti=ti->next, count++ ){
    printf( "track data length = %d\n", ti->length );
    printf( "size after extraction = %d\n", ti->extsiz );
    printf( "track data offset = %d\n", ti->offset );
  }
  printf( "%d data contained.\n", count );
}

typedef struct{
  uint8_t buf;
  uint8_t mask;
} LS11_reader_t;

#define LS11_reader_init(r) do{\
  (r)->buf = (r)->mask = 0;\
} while(0)

int LS11_read_bit(FILE *fp, LS11_reader_t *reader)
{
  int ret;

  if( reader->mask == 0 ){
    if( fread( &reader->buf, sizeof(uint8_t), 1, fp ) < 1 ) return -1;
    reader->mask = 0x80;
  }
  ret = reader->buf & reader->mask ? 1 : 0;
  reader->mask >>= 1;
  return ret;
}

int32_t LS11_decode_bit(FILE *fp, LS11_reader_t *reader)
{
  int32_t length = 0;
  int32_t val1 = 0, val2 = 0;

  do{ /* former half */
    ++length;
    val1 = ( val1 << 1 ) | LS11_read_bit( fp, reader );
  } while( val1 & 0x1 );
  do{ /* latter half */
    val2 = ( val2 << 1 ) | LS11_read_bit( fp, reader );
  } while( --length > 0 );
  return val1 + val2;
}

uint8_t *LS11_extract_byte(FILE *fp, LS11_reader_t *reader, uint8_t dictionary[], uint8_t *cur)
{
  int32_t val1, val2;

  if( ( val1 = LS11_decode_bit( fp, reader ) ) < LS11_DICTIONARY_SIZE ){
    *cur = dictionary[val1];
    return cur+1;
  }
  val1 -= LS11_DICTIONARY_SIZE;
  val2 = LS11_decode_bit( fp, reader ) + 3;
  while( --val2 >= 0 ){
    *cur = *( cur - val1 );
    cur++;
  }
  return cur;
}

uint8_t *LS11_extract_track(FILE *fin, uint8_t dictionary[], LS11_track_info_t *ti)
{
  LS11_reader_t reader;
  uint8_t *buf, *cur;

  if( !( buf = malloc( ti->extsiz ) ) ){
    fprintf( stderr, "cannot allocate memory." );
    return NULL;
  }
  LS11_reader_init( &reader );
  fseek( fin, ti->offset, SEEK_SET );
  for( cur=buf; cur-buf<ti->extsiz; ){
    cur = LS11_extract_byte( fin, &reader, dictionary, cur );
  }
  return buf;
}

int LS11_extract(const char filename[], int (* output)(uint8_t*,int,int,const char []))
{
  FILE *fin;
  LS11_header_t header;
  LS11_track_info_t *ti;
  uint8_t *buf = NULL;
  int count, ret = 0;

  if( !( fin = fopen( filename, "rb" ) ) ){
    fprintf( stderr, "cannot open file %s.\n", filename );
    return -1;
  }
  if( LS11_read_header( fin, &header ) < 0 ){
    ret = -1;
    goto TERMINATE;
  }
#if 0
  LS11_header_write( &header ); /* for debug */
#endif
  for( count=0, ti=header.track_info; ti; count++, ti=ti->next ){
    if( !( buf = LS11_extract_track( fin, header.dictionary, ti ) ) ){
      ret = -1;
      goto TERMINATE;
    }
    output( buf, count, ti->extsiz, filename );
    free( buf );
  }
 TERMINATE:
  fclose( fin );
  return ret;
}

/* nb3-graphics-specific implementation */

typedef struct{
  uint8_t b;
  uint8_t g;
  uint8_t r;
  uint8_t a; /* unused */
} nb3palette_t;

#define NB3_PALETTE_ID   1
#define NB3_NCOLOR     256

nb3palette_t palette[NB3_NCOLOR];

int nb3cut_read_palette(uint8_t *buf, int count, int size, const char filename[])
{
  register int i;

  if( count != NB3_PALETTE_ID ) return 0;
  for( i=0; i<NB3_NCOLOR; i++ ){
    palette[i].a = 0;
    palette[i].b = *buf++;
    palette[i].r = *buf++;
    palette[i].g = *buf++;
  }
  return 0;
}

#define NB3_WIDTH          64
#define NB3_HEIGHT         80
#define NB3_BPP             1 /* byte per pixel */
#define NB3_HEADERSIZE     54
#define NB3_INFOHEADERSIZE 40
#define NB3_BITCOUNT        ( NB3_BPP * 8 )
#define NB3_PPM          2834 /* pixels per meter */

void nb3_write_val(FILE *fp, size_t size, uint32_t val)
{
  for( ; size>0; size--, val>>=8 )
    fputc( val & 0xff, fp );
}

void nb3cut_output_figure_bmp(FILE *fp, uint8_t *buf, int size)
{
  uint32_t bpl, rest;
  size_t imgsize, offset;
  register int i;

  /* header charactor */
  fputc( 'B', fp );
  fputc( 'M', fp );
  /* image size calculation */
  if( ( rest = ( bpl = NB3_WIDTH * NB3_BPP ) % 4 ) ) bpl += 4 - rest; /* padding */
  imgsize = bpl * NB3_HEIGHT;
  offset = NB3_HEADERSIZE + NB3_NCOLOR * 4;
  /* file size */
  nb3_write_val( fp, 4, offset + imgsize );
  nb3_write_val( fp, 4, 0 ); /* reserved */
  nb3_write_val( fp, 4, offset ); /* data offset */
  /* info header size */
  nb3_write_val( fp, 4, NB3_INFOHEADERSIZE );
  nb3_write_val( fp, 4, NB3_WIDTH ); /* image width */
  nb3_write_val( fp, 4, NB3_HEIGHT ); /* image height */
  nb3_write_val( fp, 2, 1 ); /* number of plane */
  nb3_write_val( fp, 2, NB3_BITCOUNT ); /* bit count */
  nb3_write_val( fp, 4, 0 ); /* no compression */
  nb3_write_val( fp, 4, imgsize ); /* image size */
  nb3_write_val( fp, 4, NB3_PPM ); /* X pixels/meter */
  nb3_write_val( fp, 4, NB3_PPM ); /* Y pixels/meter */
  nb3_write_val( fp, 4, NB3_NCOLOR ); /* number of actually used colors */
  nb3_write_val( fp, 4, 0 ); /* number of important color */
  /* palette */
  for( i=0; i<NB3_NCOLOR; i++ )
    fwrite( &palette[i], 4, 1, fp );
  /* pixel */
  for( i=size-1; i>=6; i-- )
    fwrite( buf+i, sizeof(uint8_t), 1, fp );
}

static char *nb3name[] = {
  "上杉謙信",
  "里見義尭",
  "武田信玄",
  "北条氏康",
  "今川義元",
  "畠山義綱",
  "神保長職",
  "姉小路良頼",
  "本願寺光佐",
  "朝倉義景",
  "斎藤義竜",
  "織田信長",
  "徳川家康",
  "浅井長政",
  "六角義賢",
  "北畠具教",
  "足利義輝",
  "鈴木佐大夫",
  "三好長慶",
  "波多野秀治",
  "一色義道",
  "山名豊国",
  "赤松義祐",
  "尼子晴久",
  "宇喜多直家",
  "毛利元就",
  "河野通宣",
  "長宗我部元親",
  "上杉景勝",
  "武田勝頼",
  "毛利輝元",
  "河野通直",
  "北条氏政",
  "羽柴秀吉",
  "明智光秀",
  "柴田勝家",
  "山本勘助",
  "服部半蔵",
  "松永久秀",
  "足利義昭",
  "",
  "今川氏真",
  "",
  "姉小路頼綱",
  "斎藤龍興",
  "織田信忠",
  "滝川一益",
  "丹羽長秀",
  "前田利家",
  "石川数正",
  "本多忠勝",
  "細川藤孝",
  "鈴木重秀",
  "小早川隆景",
  "吉川元春",
  "毛利隆元",
  "清水宗春",
  "山中鹿介",
  "",
  "筒井順慶",
  "",
  "真田昌幸",
  "木曾義昌",
  "小山田信繁",
  "",
  "佐久間盛政",
  "",
  "",
  "",
  "風魔小太郎",
  "織田信雄",
  "黒田官兵衛",
  "竹中半兵衛",
  "宇佐美定満",
  "北条氏照",
  "佐久間信盛",
  "山県昌景",
  "高坂昌信",
  "武田信繁",
  "内藤信豊",
  "馬場信房",
  "森蘭丸",
  "",
  "真田幸村",
  "真田幸隆",
  "武田義信",
  "里見義頼",
  "北条氏規",
  "北条氏邦",
  "",
  "武田信廉",
  "穴山信君",
  "秋山信友",
  "酒井忠次",
  "稲葉一鉄",
  "織田勝長",
  "織田信包",
  "織田信孝",
  "",
  "",
  "加藤清正",
  "福島正則",
  "斎藤朝信",
  "上杉景信",
  "本庄繁長",
  "上条政繁",
  "蒲生氏郷",
  "藤堂高虎",
  "香宗我部親泰",
  "",
  "北条綱成",
  "香川親和",
  "羽柴秀長",
  "",
  "",
  "三好義賢",
  "榊原康政",
  "",
  "鳥居元忠",
  "直江景綱",
  "柿崎景家",
  "吉川広家",
  "",
  "",
  "氏家卜全",
  "安藤守就",
  "本願寺光寿",
  "",
  "細川忠興",
  "十河一存",
  "村上義清",
  "土岐頼次",
  "尼子義久",
  "北条高広",
  "里見義弘",
  "朝比奈泰朝",
  "畠山義慶",
  "神保長住",
  "姉小路信綱",
  "朝倉景健",
  "朝倉景鏡",
  "六角義治",
  "木造具政",
  "和田惟政",
  "三好長逸",
  "三好政康",
  "十河存保",
  "岩成友通",
  "一色義定",
  "山名祐豊",
  "赤松則房",
  "宇喜多忠家",
  "蜂須賀正勝",
  "口羽通良",
  "池田恒興",
  "金森長近",
  "",
};

#define FINNAME0 "palette.nb3"
#define FINNAME1 "Kao.nb3"
#define FINNAME2 "Kao2.nb3"
#define FINNAME3 "Kao3.nb3"

int nb3cut_output_track_figure(uint8_t *buf, int count, int size, const char filename[])
{
  FILE *fout;
  char trackname[BUFSIZ];

  if( strcmp( filename, FINNAME1 ) == 0 )
    sprintf( trackname, "%s.%03d%s.bmp", filename, count, nb3name[count] );
  else
    sprintf( trackname, "%s.%03d.bmp", filename, count );
  if( !( fout = fopen( trackname, "wb" ) ) ){
    fprintf( stderr, "cannot create file %s.\n", trackname );
    return -1;
  }
  nb3cut_output_figure_bmp( fout, buf, size );
  fclose( fout );
  return 0;
}

int main(int argc, char *argv[])
{
  /* palette data */
  if( LS11_extract( FINNAME0, nb3cut_read_palette ) < 0 )
    return EXIT_FAILURE;
  /* portrait data */
  if( LS11_extract( FINNAME1, nb3cut_output_track_figure ) < 0 ||
      LS11_extract( FINNAME2, nb3cut_output_track_figure ) < 0 ||
      LS11_extract( FINNAME3, nb3cut_output_track_figure ) < 0 )
    return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
