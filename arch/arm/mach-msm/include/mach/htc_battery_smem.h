/* 	Battery Temperature

	For calculating the battery temperature we could use the fpu emu or even do it completely with integer math or do a lookup table.
	The result for all 3 options would be exactly the same, all with pros and cons regarding code size or table size. Using a simple
	lookup table is for the moment the best sollution as its fastest and smallest of the 3 options.

	For completeness on how to calculate the battery temperature from scrach here is the used formula.
	formula:
	// x is the raw info we get from smem.
	int temp = ( x * 18 ) / ( 2600 - x );
	int battery_temperature = (int)( ( 1.0 / ( log( double( temp ) * 0.01 ) / 4360.0 ) + ( 1.0 / ( 273.15 + 25.0 ) ) ) - 273.15 ) * 10.0;

	int64 formula:

	#include <math64.h>

	s64 temp_x = ((s64)log_table[ temp ] ) * 1000LL;
	temp_x = (  div_s64( temp_x, 4360 ) ) + 3354016LL;
	temp_x = div64_u64( 100000000000ULL, temp_x );
	temp_x = temp_x - 27315;
	buffer->batt_temp = ( ( int ) temp_x ) / 10;

	int64 table:
	// this is the hacked formula only using integer math
	log_table table is created by using the following code to dump a table
	long long temp = ( i * 18 ) / ( 2600 - i );
	int d_log = (int) log( double( temp * 0.01 ) ) * 1000000.0;

	todo: if you look at the end of the table you see a lot of repeatating numbers, this is because of the integer math in the first
			equalation. We could decrease the table size by adding some hardcoded numbers.
*/

/*	Table created with the following program
	int GetRealPartTemp( int x ) {
		double temp_f = log( double( x ) * 0.01 );
		temp_f = temp_f * ( 1.0 / 4360.0 );
		temp_f = temp_f + ( 1.0 / ( 273.15 + 25.0 ) );
		temp_f = 1.0 / temp_f;
		temp_f = temp_f - 273.15;
		return ( int )( double )( temp_f * 10.0 );
	}

	int main() {
		int table_size = 0, offset = 0, i = 0, temp = 0;
		for ( ; i < 2599; i++ ) {
			temp = GetRealPartTemp(i);

			if ( temp > 900 || temp < -200 ) continue;			// limit for not hotter than 90 degrease not colder than -20
			table_size++;
			if ( offset == 0 ) offset = i;								// save which offset we start.

			fprintf( fp, "%4d, ", temp );

			if ( !(table_size % 10) ) fprintf(fp, "\n\t");			// some formatting
		}
		fprintf(fp, "\n };");
		fprintf(fp, "// offset = %d\n", offset);
		fclose(fp);
		return 0;
	}
*/

/* Charge graph :
 *
 *      Li-ion battery charging is made in two steps :
 *      1 / Constant current charge : During this step, the voltage of the battery reach it's max value.
 *          At the end of this step, the battery charge level is about 70%
 *      2 / Constant voltage charge : During this step, the charge current will decrease until it reach
 *          3% of the rated battery capacity.
 *          The battery can be considered fully charged when the charge current is 3% or less.
 *
 *      1st part of charging process :
 *      ------------------------------
 *                     Voltage (%)
 *                          ^
 *                     100% |                                                        x
 *                          |
 *  max_volt_min_percentage |                                           x
 *                          |
 *  mid_volt_min_percentage |                                x
 *                          |
 *  low_volt_min_percentage |                    x
 *                          |
 *                       0% +--------x-------------------------------------------------> Voltage
 *                                   |           |           |           |           |
 *                            critical_volt   low_volt    mid_volt    max_volt   full_volt
 *                             _threshold   _threshold  _threshold  _threshold  _threshold
 *
 *	2nd part of charging process (C = battery capacity in current -> 900mAh batt => 900mA):
 *	------------------------------
 *                       Current
 *                          ^
 *                        C | x	x
 *                          |
 *                          |        x
 *                          |
 *                          |             x
 *                          |
 *                          |                      x
 *                     3% C |                                x
 *                          +------------------------------------> Percent of charge
 *                               |        |        |         |
 *                              70%                         100%
 *
 * added:
 * the discharging curve can be define via *6 points* and every point can be positioning on the courve (like if you would try to draw the curve via lines
 * therefore you can define a slope value and start height
 * see paramter below, and also you can find on XDA developers a XLS files where you can see the algo how it works in detail
 * also in the XLS can the values be tested (you only need log data from your model - via grep kernellog for "battlog")
*/

/* Percentage calculus :
 *
 *  - portion range in Volts is ( #next portion#_threshold - #current portion#_threshold )
 *  - portion range in % is ( #next portion#_min_percentage - #current portion#_min_percentage )
 *
 *  - % of charge in the current portion of the graph = ( current voltage - #current portion#_threshold) / (portion range in Volts)
 *		This value will be between 0 and 1
 *  - % of charge in the global percentage = % of charge in the current portion of the graph *  (#current portion#_perc_range)
 *		This value will be between 0 and (#current portion#_perc_range)
 *  *****************************************************************************************
 *  * % of charge = % of charge in the global percentage + #current portion#_min_percentage *
 *  *****************************************************************************************
 */

/* Temperature lookup table by Cardsharing
 * /!\ for photon only!
 */
static const short temp_table[] = {
          637,   637,   637,   637,   636,   636,   636,   635,   635,   635,   
          635,   634,   634,   634,   633,   633,   633,   633,   632,   632,   
          632,   631,   631,   631,   631,   630,   630,   630,   629,   629,   
          629,   629,   628,   628,   628,   627,   627,   627,   627,   626,   
          626,   626,   625,   625,   625,   625,   624,   624,   624,   623,   
          623,   623,   623,   622,   622,   622,   621,   621,   621,   621,   
          620,   620,   620,   619,   619,   619,   619,   618,   618,   618,   
          617,   617,   617,   617,   616,   616,   616,   615,   615,   615,   
          615,   614,   614,   614,   613,   613,   613,   613,   612,   612,   
          612,   611,   611,   611,   611,   610,   610,   610,   609,   609,   
          609,   609,   608,   608,   608,   607,   607,   607,   607,   606,   
          606,   606,   605,   605,   605,   605,   604,   604,   604,   603,   
          603,   603,   603,   602,   602,   602,   601,   601,   601,   601,   
          600,   600,   600,   599,   599,   599,   599,   598,   598,   598,   
          597,   597,   597,   597,   596,   596,   596,   595,   595,   595,   
          595,   594,   594,   594,   593,   593,   593,   593,   592,   592,   
          592,   591,   591,   591,   591,   590,   590,   590,   589,   589,   
          589,   589,   588,   588,   588,   587,   587,   587,   587,   586,   
          586,   586,   585,   585,   585,   585,   584,   584,   584,   583,   
          583,   583,   583,   582,   582,   582,   581,   581,   581,   581,   
          580,   580,   580,   579,   579,   579,   579,   578,   578,   578,   
          577,   577,   577,   577,   576,   576,   576,   575,   575,   575,   
          575,   574,   574,   574,   573,   573,   573,   573,   572,   572,   
          572,   571,   571,   571,   571,   570,   570,   570,   569,   569,   
          569,   569,   568,   568,   568,   567,   567,   567,   567,   566,   
          566,   566,   565,   565,   565,   565,   564,   564,   564,   563,   
          563,   563,   563,   562,   562,   562,   561,   561,   561,   561,   
          560,   560,   560,   559,   559,   559,   559,   558,   558,   558,   
          557,   557,   557,   557,   556,   556,   556,   555,   555,   555,   
          555,   554,   554,   554,   553,   553,   553,   553,   552,   552,   
          552,   551,   551,   551,   551,   550,   550,   550,   549,   549,   
          549,   549,   548,   548,   548,   547,   547,   547,   547,   546,   
          546,   546,   545,   545,   545,   545,   544,   544,   544,   543,   
          543,   543,   543,   542,   542,   542,   541,   541,   541,   541,   
          540,   540,   540,   539,   539,   539,   539,   538,   538,   538,   
          537,   537,   537,   537,   536,   536,   536,   535,   535,   535,   
          535,   534,   534,   534,   533,   533,   533,   533,   532,   532,   
          532,   531,   531,   531,   531,   530,   530,   530,   529,   529,   
          529,   529,   528,   528,   528,   527,   527,   527,   527,   526,   
          526,   526,   525,   525,   525,   525,   524,   524,   524,   523,   
          523,   523,   523,   522,   522,   522,   521,   521,   521,   521,   
          520,   520,   520,   519,   519,   519,   519,   518,   518,   518,   
          517,   517,   517,   517,   516,   516,   516,   515,   515,   515,   
          515,   514,   514,   514,   513,   513,   513,   513,   512,   512,   
          512,   511,   511,   511,   511,   510,   510,   510,   509,   509,   
          509,   509,   508,   508,   508,   507,   507,   507,   507,   506,   
          506,   506,   505,   505,   505,   505,   504,   504,   504,   503,   
          503,   503,   503,   502,   502,   502,   501,   501,   501,   501,   
          500,   500,   500,   499,   499,   499,   499,   498,   498,   498,   
          497,   497,   497,   497,   496,   496,   496,   495,   495,   495,   
          495,   494,   494,   494,   493,   493,   493,   493,   492,   492,   
          492,   491,   491,   491,   491,   490,   490,   490,   489,   489,   
          489,   489,   488,   488,   488,   487,   487,   487,   487,   486,   
          486,   486,   485,   485,   485,   485,   484,   484,   484,   483,   
          483,   483,   483,   482,   482,   482,   481,   481,   481,   481,   
          480,   480,   480,   479,   479,   479,   479,   478,   478,   478,   
          477,   477,   477,   477,   476,   476,   476,   475,   475,   475,   
          475,   474,   474,   474,   473,   473,   473,   473,   472,   472,   
          472,   471,   471,   471,   471,   470,   470,   470,   469,   469,   
          469,   469,   468,   468,   468,   467,   467,   467,   467,   466,   
          466,   466,   465,   465,   465,   465,   464,   464,   464,   463,   
          463,   463,   463,   462,   462,   462,   461,   461,   461,   461,   
          460,   460,   460,   459,   459,   459,   459,   458,   458,   458,   
          457,   457,   457,   457,   456,   456,   456,   455,   455,   455,   
          455,   454,   454,   454,   453,   453,   453,   453,   452,   452,   
          452,   451,   451,   451,   451,   450,   450,   450,   449,   449,   
          449,   449,   448,   448,   448,   447,   447,   447,   447,   446,   
          446,   446,   445,   445,   445,   445,   444,   444,   444,   443,   
          443,   443,   443,   442,   442,   442,   441,   441,   441,   441,   
          440,   440,   440,   439,   439,   439,   439,   438,   438,   438,   
          437,   437,   437,   437,   436,   436,   436,   435,   435,   435,   
          435,   434,   434,   434,   433,   433,   433,   433,   432,   432,   
          432,   431,   431,   431,   431,   430,   430,   430,   429,   429,   
          429,   429,   428,   428,   428,   427,   427,   427,   427,   426,   
          426,   426,   425,   425,   425,   425,   424,   424,   424,   423,   
          423,   423,   423,   422,   422,   422,   421,   421,   421,   421,   
          420,   420,   420,   419,   419,   419,   419,   418,   418,   418,   
          417,   417,   417,   417,   416,   416,   416,   415,   415,   415,   
          415,   414,   414,   414,   413,   413,   413,   413,   412,   412,   
          412,   411,   411,   411,   411,   410,   410,   410,   409,   409,   
          409,   409,   408,   408,   408,   407,   407,   407,   407,   406,   
          406,   406,   405,   405,   405,   405,   404,   404,   404,   403,   
          403,   403,   403,   402,   402,   402,   401,   401,   401,   401,   
          400,   400,   400,   399,   399,   399,   399,   398,   398,   398,   
          397,   397,   397,   397,   396,   396,   396,   395,   395,   395,   
          395,   394,   394,   394,   393,   393,   393,   393,   392,   392,   
          392,   391,   391,   391,   391,   390,   390,   390,   389,   389,   
          389,   389,   388,   388,   388,   387,   387,   387,   387,   386,   
          386,   386,   385,   385,   385,   385,   384,   384,   384,   383,   
          383,   383,   383,   382,   382,   382,   381,   381,   381,   381,   
          380,   380,   380,   379,   379,   379,   379,   378,   378,   378,   
          377,   377,   377,   377,   376,   376,   376,   375,   375,   375,   
          375,   374,   374,   374,   373,   373,   373,   373,   372,   372,   
          372,   371,   371,   371,   371,   370,   370,   370,   369,   369,   
          369,   369,   368,   368,   368,   367,   367,   367,   367,   366,   
          366,   366,   365,   365,   365,   365,   364,   364,   364,   363,   
          363,   363,   363,   362,   362,   362,   361,   361,   361,   361,   
          360,   360,   360,   359,   359,   359,   359,   358,   358,   358,   
          357,   357,   357,   357,   356,   356,   356,   355,   355,   355,   
          355,   354,   354,   354,   353,   353,   353,   353,   352,   352,   
          352,   351,   351,   351,   351,   350,   350,   350,   349,   349,   
          349,   349,   348,   348,   348,   347,   347,   347,   347,   346,   
          346,   346,   345,   345,   345,   345,   344,   344,   344,   343,   
          343,   343,   343,   342,   342,   342,   341,   341,   341,   341,   
          340,   340,   340,   339,   339,   339,   339,   338,   338,   338,   
          337,   337,   337,   337,   336,   336,   336,   335,   335,   335,   
          335,   334,   334,   334,   333,   333,   333,   333,   332,   332,   
          332,   331,   331,   331,   331,   330,   330,   330,   329,   329,   
          329,   329,   328,   328,   328,   327,   327,   327,   327,   326,   
          326,   326,   325,   325,   325,   325,   324,   324,   324,   323,   
          323,   323,   323,   322,   322,   322,   321,   321,   321,   321,   
          320,   320,   320,   319,   319,   319,   319,   318,   318,   318,   
          317,   317,   317,   317,   316,   316,   316,   315,   315,   315,   
          315,   314,   314,   314,   313,   313,   313,   313,   312,   312,   
          312,   311,   311,   311,   311,   310,   310,   310,   309,   309,   
          309,   309,   308,   308,   308,   307,   307,   307,   307,   306,   
          306,   306,   305,   305,   305,   305,   304,   304,   304,   303,   
          303,   303,   303,   302,   302,   302,   301,   301,   301,   301,   
          300,   300,   300,   299,   299,   299,   299,   298,   298,   298,   
          297,   297,   297,   297,   296,   296,   296,   295,   295,   295,   
          295,   294,   294,   294,   293,   293,   293,   293,   292,   292,   
          292,   291,   291,   291,   291,   290,   290,   290,   289,   289,   
          289,   289,   288,   288,   288,   287,   287,   287,   287,   286,   
          286,   286,   285,   285,   285,   285,   284,   284,   284,   283,   
          283,   283,   283,   282,   282,   282,   281,   281,   281,   281,   
          280,   280,   280,   279,   279,   279,   279,   278,   278,   278,   
          277,   277,   277,   277,   276,   276,   276,   275,   275,   275,   
          275,   274,   274,   274,   273,   273,   273,   273,   272,   272,   
          272,   271,   271,   271,   271,   270,   270,   270,   269,   269,   
          269,   269,   268,   268,   268,   267,   267,   267,   267,   266,   
          266,   266,   265,   265,   265,   265,   264,   264,   264,   263,   
          263,   263,   263,   262,   262,   262,   261,   261,   261,   261,   
          260,   260,   260,   259,   259,   259,   259,   258,   258,   258,   
          257,   257,   257,   257,   256,   256,   256,   255,   255,   255,   
          255,   254,   254,   254,   253,   253,   253,   253,   252,   252,   
          252,   251,   251,   251,   251,   250,   250,   250,   249,   249,   
          249,   249,   248,   248,   248,   247,   247,   247,   247,   246,   
          246,   246,   245,   245,   245,   245,   244,   244,   244,   243,   
          243,   243,   243,   242,   242,   242,   241,   241,   241,   241,   
          240,   240,   240,   239,   239,   239,   239,   238,   238,   238,   
          237,   237,   237,   237,   236,   236,   236,   235,   235,   235,   
          235,   234,   234,   234,   233,   233,   233,   233,   232,   232,   
          232,   231,   231,   231,   231,   230,   230,   230,   229,   229,   
          229,   229,   228,   228,   228,   227,   227,   227,   227,   226,   
          226,   226,   225,   225,   225,   225,   224,   224,   224,   223,   
          223,   223,   223,   222,   222,   222,   221,   221,   221,   221,   
          220,   220,   220,   219,   219,   219,   219,   218,   218,   218,   
          217,   217,   217,   217,   216,   216,   216,   215,   215,   215,   
          215,   214,   214,   214,   213,   213,   213,   213,   212,   212,   
          212,   211,   211,   211,   211,   210,   210,   210,   209,   209,   
          209,   209,   208,   208,   208,   207,   207,   207,   207,   206,   
          206,   206,   205,   205,   205,   205,   204,   204,   204,   203,   
          203,   203,   203,   202,   202,   202,   201,   201,   201,   201,   
          200,   200,   200,   199,   199,   199,   199,   198,   198,   198,   
          197,   197,   197,   197,   196,   196,   196,   195,   195,   195,   
          195,   194,   194,   194,   193,   193,   193,   193,   192,   192,   
          192,   191,   191,   191,   191,   190,   190,   190,   189,   189,   
          189,   189,   188,   188,   188,   187,   187,   187,   187,   186,   
          186,   186,   185,   185,   185,   185,   184,   184,   184,   183,   
          183,   183,   183,   182,   182,   182,   181,   181,   181,   181,   
          180,   180,   180,   179,   179,   179,   179,   178,   178,   178,   
          177,   177,   177,   177,   176,   176,   176,   175,   175,   175,   
          175,   174,   174,   174,   173,   173,   173,   173,   172,   172,   
          172,   171,   171,   171,   171,   170,   170,   170,   169,   169,   
          169,   169,   168,   168,   168,   167,   167,   167,   167,   166,   
          166,   166,   165,   165,   165,   165,   164,   164,   164,   163,   
          163,   163,   163,   162,   162,   162,   161,   161,   161,   161,   
          160,   160,   160,   159,   159,   159,   159,   158,   158,   158,   
          157,   157,   157,   157,   156,   156,   156,   155,   155,   155,   
          155,   154,   154,   154,   153,   153,   153,   153,   152,   152,   
          152,   151,   151,   151,   151,   150,   150,   150,   149,   149,   
          149,   149,   148,   148,   148,   147,   147,   147,   147,   146,   
          146,   146,   145,   145,   145,   145,   144,   144,   144,   143,   
          143,   143,   143,   142,   142,   142,   141,   141,   141,   141,   
          140,   140,   140,   139,   139,   139,   139,   138,   138,   138,   
          137,   137,   137,   137,   136,   136,   136,   135,   135,   135,   
          135,   134,   134,   134,   133,   133,   133,   133,   132,   132,   
          132,   131,   131,   131,   131,   130,   130,   130,   129,   129,   
          129,   129,   128,   128,   128,   127,   127,   127,   127,   126,   
          126,   126,   125,   125,   125,   125,   124,   124,   124,   123,   
          123,   123,   123,   122,   122,   122,   121,   121,   121,   121,   
          120,   120,   120,   119,   119,   119,   119,   118,   118,   118,   
          117,   117,   117,   117,   116,   116,   116,   115,   115,   115,   
          115,   114,   114,   114,   113,   113,   113,   113,   112,   112,   
          112,   111,   111,   111,   111,   110,   110,   110,   109,   109,   
          109,   109,   108,   108,   108,   107,   107,   107,   107,   106,   
          106,   106,   105,   105,   105,   105,   104,   104,   104,   103,   
          103,   103,   103,   102,   102,   102,   101,   101,   101,   101,   
          100,   100,   100,    99,    99,    99,    99,    98,    98,    98,   
           97,    97,    97,    97,    96,    96,    96,    95,    95,    95,   
           95,    94,    94,    94,    93,    93,    93,    93,    92,    92,   
           92,    91,    91,    91,    91,    90,    90,    90,    89,    89,   
           89,    89,    88,    88,    88,    87,    87,    87,    87,    86,   
           86,    86,    85,    85,    85,    85,    84,    84,    84,    83,   
           83,    83,    83,    82,    82,    82,    81,    81,    81,    81,   
           80,    80,    80,    79,    79,    79,    79,    78,    78,    78,   
           77,    77,    77,    77,    76,    76,    76,    75,    75,    75,   
           75,    74,    74,    74,    73,    73,    73,    73,    72,    72,   
           72,   71,   71,   71,   71,   70,   70,   70,   69,   69,   
           69,   69,   68,   68,   68,   67,   67,   67,   67,   66,   
           66,   66,   65,   65,   65,   65,   64,   64,   64,   63,   
           63,   63,   63,   62,   62,   62,   61,   61,   61,   61,   
           60,   60,   60,   59,   59,   59,   59,   58,   58,   58,   
           57,   57,   57,   57,   56,   56,   56,   55,   55,   55,   
           55,   54,   54,   54,   53,   53,   53,   53,   52,   52,   
           52,   51,   51,   51,   51,   50,   50,   50,   49,   49,   
           49,   49,   48,   48,   48,   47,   47,   47,   47,   46,   
           46,   46,   45,   45,   45,   45,   44,   44,   44,   43,   
           43,   43,   43,   42,   42,   42,   41,   41,   41,   41,   
           40,   40,   40,   39,   39,   39,   39,   38,   38,   38,   
           37,   37,   37,   37,   36,   36,   36,   35,   35,   35,   
           35,   34,   34,   34,   33,   33,   33,   33,   32,   32,   
           32,   31,   31,   31,   31,   30,   30,   30,   29,   29,   
           29,   29,   28,   28,   28,   27,   27,   27,   27,   26,   
           26,   26,   25,   25,   25,   25,   24,   24,   24,   23,   
           23,   23,   23,   22,   22,   22,   21,   21,   21,   21,   
           20,   20,   20,   19,   19,   19,   19,   18,   18,   18,   
           17,   17,   17,   17,   16,   16,   16,   15,   15,   15,   
           15,   14,   14,   14,   13,   13,   13,   13,   12,   12,   
           12,   11,   11,   11,   11,   10,   10,   10,   9,   9,   
            9,   9,   8,   8,   8,   7,   7,   7,   7,   6,   
            6,   6,   5,   5,   5,   5,   4,   4,   4,   3,   
            3,   3,   3,   2,   2,   2,   1,   1,   1,   1,   
            0,   0,   -0,   -1,   -1,   -1,   -1,   -2,   -2,   -2,   
           -3,   -3,   -3,   -3,   -4,   -4,   -4,   -5,   -5,   -5,   
           -5,   -6,   -6,   -6,   -7,   -7,   -7,   -7,   -8,   -8,   
           -8,   -9,   -9,   -9,   -9,   -10,   -10,   -10,   -11,   -11,   
          -11,   -11,   -12,   -12,   -12,   -13,   -13,   -13,   -13,   -14,   
          -14,   -14,   -15,   -15,   -15,   -15,   -16,   -16,   -16,   -17,   
          -17,   -17,   -17,   -18,   -18,   -18,   -19,   -19,   -19,   -19,   
          -20,   -20,   -20,   -21,   -21,   -21,   -21,   -22,   -22,   -22,   
          -23,   -23,   -23,   -23,   -24,   -24,   -24,   -25,   -25,   -25,   
          -25,   -26,   -26,   -26,   -27,   -27,   -27,   -27,   -28,   -28,   
          -28,   -29,   -29,   -29,   -29,   -30,   -30,   -30,   -31,   -31,   
          -31,   -31,   -32,   -32,   -32,   -33,   -33,   -33,   -33,   -34,   
          -34,   -34,   -35,   -35,   -35,   -35,   -36,   -36,   -36,   -37,   
          -37,   -37,   -37,   -38,   -38,   -38,   -39,   -39,   -39,   -39,   
          -40,   -40,   -40,   -41,   -41,   -41,   -41,   -42,   -42,   -42,   
          -43,   -43,   -43,   -43,   -44,   -44,   -44,   -45,   -45,   -45,   
          -45,   -46,   -46,   -46,   -47,   -47,   -47,   -47,   -48,   -48,   
          -48,   -49,   -49,   -49,   -49,   -50,   -50,   -50,   -51,   -51,   
          -51,   -51,   -52,   -52,   -52,   -53,   -53,   -53,   -53,   -54,   
          -54,   -54,   -55,   -55,   -55,   -55,   -56,   -56,   -56,   -57,   
          -57,   -57,   -57,   -58,   -58,   -58,   -59,   -59,   -59,   -59,   
          -60,   -60,   -60,   -61,   -61,   -61,   -61,   -62,   -62,   -62,   
          -63,   -63,   -63,   -63,   -64,   -64,   -64,   -65,   -65,   -65,   
          -65,   -66,   -66,   -66,   -67,   -67,   -67,   -67,   -68,   -68,   
          -68,   -69,   -69,   -69,   -69,   -70,   -70,   -70,   -71,   -71,   
          -71,   -71,   -72,   -72,   -72,   -73,   -73,   -73,   -73,   -74,   
          -74,   -74,   -75,   -75,   -75,   -75,   -76,   -76,   -76,   -77,   
          -77,   -77,   -77,   -78,   -78,   -78,   -79,   -79,   -79,   -79,   
          -80,   -80,   -80,   -81,   -81,   -81,   -81,   -82,   -82,   -82,   
          -83,   -83,   -83,   -83,   -84,   -84,   -84,   -85,   -85,   -85,   
          -85,   -86,   -86,   -86,   -87,   -87,   -87,   -87,   -88,   -88,   
          -88,   -89,   -89,   -89,   -89,   -90,   -90,   -90,   -91,   -91,   
          -91,   -91,   -92,   -92,   -92,   -93,   -93,   -93,   -93,   -94,   
          -94,   -94,   -95,   -95,   -95,   -95,   -96,   -96,   -96,   -97,   
          -97,   -97,   -97,   -98,   -98,   -98,   -99,   -99,   -99,   -99,   
          -100,   -100,   -100,   -101,   -101,   -101,   -101,   -102,   -102,   -102,   
          -103,   -103,   -103,   -103,   -104,   -104,   -104,   -105,   -105,   -105,   -105
 };

struct sBattery_Parameters
{
	/* Charge graph :
	 * 	See htc_battery_smem.h for details
	 * see Battery_Calculator.xls -> can be found on XDA-Developers
	 */                                                            
	int battery_capacity;               /* Battery capacity (mAh) */
	int termination_current;            /* Charge termination current (typically 3% of battery_capacity) */
	int temp_correction;
	int temp_correction_const;
	int volt_discharge_res_coeff;

        //definition of points
	int cri_volt_threshold;        	    /* Point 1 - Lowest voltage value the critical voltage part of the graph */
	int low_volt_threshold;             /* Point 2 Lowest voltage value the low voltage part of the graph */
	int min_volt_threshold;             /* Point 3 Mininimum voltage value the mid voltage part of the graph */
	int mid_volt_threshold;             /* Point 4 Lowest voltage value the mid voltage part of the graph */
	int med_volt_threshold;             /* Point 5 Lowest voltage value the mid voltage part of the graph */
	int max_volt_threshold;             /* Point 6 Lowest voltage value the max voltage part of the graph */
	int full_volt_threshold;     /* only for define when battery is on 99% or higher almost full voltage of the battery */
	// percent cure start min percentage (% x 10)
	int cri_volt_perc_start; 
	int low_volt_perc_start;
	int min_volt_perc_start;
	int mid_volt_perc_start;
	int med_volt_perc_start;
	int max_volt_perc_start;
	//dynamic slope (centi-unti) (unit x 10)
	int cri_volt_dynslope;
	int low_volt_dynslope;
	int min_volt_dynslope;
	int mid_volt_dynslope;
	int med_volt_dynslope;
	int max_volt_dynslope;
} ;


/* Extracted from battdrvr.dll ver.2008.6.22.0 of HTC topaz
 * Batt Vendor 1
 */
static const struct sBattery_Parameters sBatParams_Topaz_1100mAh_v1 =
{
    .battery_capacity =			1100,
    .termination_current =		70,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3400,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3700,
    .mid_volt_threshold =		3735,
    .med_volt_threshold =		3800,
    .max_volt_threshold =		4050,
    .full_volt_threshold =		4110,
    .cri_volt_perc_start =		-10,
    .low_volt_perc_start =		75,
    .min_volt_perc_start =		300,
    .mid_volt_perc_start =		420,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		930,
    .cri_volt_dynslope =		220,
    .low_volt_dynslope =		44,
    .min_volt_dynslope =		28,
    .mid_volt_dynslope =		43,
    .med_volt_dynslope =		68,
    .max_volt_dynslope =		96,
};

/* Extracted from battdrvr.dll ver.2008.6.22.0 of HTC topaz
 * Batt Vendor 2 and >5
 */
static const struct sBattery_Parameters sBatParams_Topaz_1100mAh_v2 =
{
    .battery_capacity =			1100,
    .termination_current =		70,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3340,
    .low_volt_threshold =		3615,
    .min_volt_threshold =		3700,
    .mid_volt_threshold =		3750,
    .med_volt_threshold =		3820,
    .max_volt_threshold =		3980,
    .full_volt_threshold =		4160,
    .cri_volt_perc_start =		0,
    .low_volt_perc_start =		45,
    .min_volt_perc_start =		220,
    .mid_volt_perc_start =		405,
    .med_volt_perc_start =		585,
    .max_volt_perc_start =		805,
    .cri_volt_dynslope =		600,
    .low_volt_dynslope =		48,
    .min_volt_dynslope =		27,
    .mid_volt_dynslope =		39,
    .med_volt_dynslope =		73,
    .max_volt_dynslope =		95,
};


/* Extracted from battdrvr.dll ver.2008.6.22.0 of HTC topaz
 * Batt Vendor 3
 */
static const struct sBattery_Parameters sBatParams_Topaz_1350mAh_v3 =
{
    .battery_capacity =			1350,
    .termination_current =		33,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3400,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3700,
    .mid_volt_threshold =		3735,
    .med_volt_threshold =		3800,
    .max_volt_threshold =		4050,
    .full_volt_threshold =		4110,
    .cri_volt_perc_start =		-10,
    .low_volt_perc_start =		75,
    .min_volt_perc_start =		300,
    .mid_volt_perc_start =		420,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		930,
    .cri_volt_dynslope =		220,
    .low_volt_dynslope =		44,
    .min_volt_dynslope =		28,
    .mid_volt_dynslope =		43,
    .med_volt_dynslope =		68,
    .max_volt_dynslope =		96,
};

/* Extracted from battdrvr.dll ver.2008.6.22.0 of HTC topaz
 * Batt Vendor 4
 */
static const struct sBattery_Parameters sBatParams_Topaz_1350mAh_v4 =
{
    .battery_capacity =			1350,
    .termination_current =		33,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3400,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3700,
    .mid_volt_threshold =		3735,
    .med_volt_threshold =		3800,
    .max_volt_threshold =		4050,
    .full_volt_threshold =		4110,
    .cri_volt_perc_start =		-10,
    .low_volt_perc_start =		75,
    .min_volt_perc_start =		300,
    .mid_volt_perc_start =		420,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		930,
    .cri_volt_dynslope =		220,
    .low_volt_dynslope =		44,
    .min_volt_dynslope =		28,
    .mid_volt_dynslope =		43,
    .med_volt_dynslope =		68,
    .max_volt_dynslope =		96,
};

/* Extracted from battdrvr.dll ver.2008.6.22.0 of HTC topaz
 * Batt Vendor 5
 */
static const struct sBattery_Parameters sBatParams_Topaz_2150mAh =
{
    .battery_capacity =			2150,
    .termination_current =		33,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3400,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3700,
    .mid_volt_threshold =		3735,
    .med_volt_threshold =		3800,
    .max_volt_threshold =		4050,
    .full_volt_threshold =		4110,
    .cri_volt_perc_start =		-10,
    .low_volt_perc_start =		75,
    .min_volt_perc_start =		300,
    .mid_volt_perc_start =		420,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		930,
    .cri_volt_dynslope =		220,
    .low_volt_dynslope =		44,
    .min_volt_dynslope =		28,
    .mid_volt_dynslope =		43,
    .med_volt_dynslope =		68,
    .max_volt_dynslope =		96,
};

/* Extracted from battdrvr.dll ver.5.8.0.0 of HTC Raphael */
static const struct sBattery_Parameters sBatParams_Raphael_1800mAh =
{
    .battery_capacity =			2150,
    .termination_current =		33,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3400,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3700,
    .mid_volt_threshold =		3735,
    .med_volt_threshold =		3800,
    .max_volt_threshold =		4050,
    .full_volt_threshold =		4110,
    .cri_volt_perc_start =		-10,
    .low_volt_perc_start =		75,
    .min_volt_perc_start =		300,
    .mid_volt_perc_start =		420,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		930,
    .cri_volt_dynslope =		220,
    .low_volt_dynslope =		44,
    .min_volt_dynslope =		28,
    .mid_volt_dynslope =		43,
    .med_volt_dynslope =		68,
    .max_volt_dynslope =		96,
};

//for blackstone original akku pack 1350
static const struct sBattery_Parameters sBatParams_Blackstone_1350mAh =
{
    .battery_capacity =			1350,
    .termination_current =		75,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3300,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3720,
    .mid_volt_threshold =		3755,
    .med_volt_threshold =		3820,
    .max_volt_threshold =		4020,
    .full_volt_threshold =		4150,
    .cri_volt_perc_start =		-40,
    .low_volt_perc_start =		70,
    .min_volt_perc_start =		245,
    .mid_volt_perc_start =		390,
    .med_volt_perc_start =		555,
    .max_volt_perc_start =		860,
    .cri_volt_dynslope =		270,
    .low_volt_dynslope =		70,
    .min_volt_dynslope =		24,
    .mid_volt_dynslope =		39,
    .med_volt_dynslope =		65,
    .max_volt_dynslope =		100,
};

//camel - i add it for blackstone
static const struct sBattery_Parameters sBatParams_Blackstone_1500mAh =
{
    .battery_capacity =			1500,
    .termination_current =		75,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3300,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3720,
    .mid_volt_threshold =		3755,
    .med_volt_threshold =		3820,
    .max_volt_threshold =		4020,
    .full_volt_threshold =		4180,
    .cri_volt_perc_start =		10,
    .low_volt_perc_start =		105,
    .min_volt_perc_start =		260,
    .mid_volt_perc_start =		390,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		860,
    .cri_volt_dynslope =		200,
    .low_volt_dynslope =		65,
    .min_volt_dynslope =		26,
    .mid_volt_dynslope =		38,
    .med_volt_dynslope =		70,
    .max_volt_dynslope =		110,
};

static const struct sBattery_Parameters sBatParams_900mAh =
{
    .battery_capacity =			900,
    .termination_current =		80,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3400,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3700,
    .mid_volt_threshold =		3735,
    .med_volt_threshold =		3800,
    .max_volt_threshold =		4000,
    .full_volt_threshold =		4140,
    .cri_volt_perc_start =		10,
    .low_volt_perc_start =		105,
    .min_volt_perc_start =		260,
    .mid_volt_perc_start =		390,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		860,
    .cri_volt_dynslope =		200,
    .low_volt_dynslope =		65,
    .min_volt_dynslope =		26,
    .mid_volt_dynslope =		38,
    .med_volt_dynslope =		70,
    .max_volt_dynslope =		110,
};

/* Same values in battdrvr.dll from diamond and raphael */
static const struct sBattery_Parameters sBatParams_1340mAh =
{
    .battery_capacity =			1340,
    .termination_current =		53,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3340,
    .low_volt_threshold =		3615,
    .min_volt_threshold =		3680,
    .mid_volt_threshold =		3750,
    .med_volt_threshold =		3815,
    .max_volt_threshold =		3950,
    .full_volt_threshold =		4155,
    .cri_volt_perc_start =		15,
    .low_volt_perc_start =		65,
    .min_volt_perc_start =		195,
    .mid_volt_perc_start =		405,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		765,
    .cri_volt_dynslope =		600,
    .low_volt_dynslope =		48,
    .min_volt_dynslope =		34,
    .mid_volt_dynslope =		41,
    .med_volt_dynslope =		69,
    .max_volt_dynslope =		83,
};

static const struct sBattery_Parameters sBatParams_1800mAh =
{
    .battery_capacity =			1800,
    .termination_current =		33,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3400,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3700,
    .mid_volt_threshold =		3735,
    .med_volt_threshold =		3800,
    .max_volt_threshold =		4000,
    .full_volt_threshold =		4140,
    .cri_volt_perc_start =		10,
    .low_volt_perc_start =		105,
    .min_volt_perc_start =		260,
    .mid_volt_perc_start =		390,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		860,
    .cri_volt_dynslope =		200,
    .low_volt_dynslope =		65,
    .min_volt_dynslope =		26,
    .mid_volt_dynslope =		38,
    .med_volt_dynslope =		70,
    .max_volt_dynslope =		110,
};

static const struct sBattery_Parameters sBatParams_Kovsky_1500mAh =
{
    .battery_capacity =			1500,
    .termination_current =		85,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3300,
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3720,
    .mid_volt_threshold =		3755,
    .med_volt_threshold =		3820,
    .max_volt_threshold =		4020,
    .full_volt_threshold =		4180,
    .cri_volt_perc_start =		10,
    .low_volt_perc_start =		105,
    .min_volt_perc_start =		260,
    .mid_volt_perc_start =		390,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		860,
    .cri_volt_dynslope =		200,
    .low_volt_dynslope =		65,
    .min_volt_dynslope =		26,
    .mid_volt_dynslope =		38,
    .med_volt_dynslope =		70,
    .max_volt_dynslope =		110,
};

static const struct sBattery_Parameters sBatParams_Kovsky_2000mAh =
{
	.battery_capacity		= 1500,
	.termination_current		= 85,
	.temp_correction		= 0,
	.temp_correction_const		= 16,
	.volt_discharge_res_coeff	= 27,
	.cri_volt_threshold		= 3300,
	.low_volt_threshold		= 3600,
	.min_volt_threshold		= 3720,
	.mid_volt_threshold		= 3755,
	.med_volt_threshold		= 3820,
	.max_volt_threshold		= 4020,
	.full_volt_threshold		= 4180,
	.cri_volt_perc_start		= 10,
	.low_volt_perc_start		= 105,
	.min_volt_perc_start		= 260,
	.mid_volt_perc_start		= 390,
	.med_volt_perc_start		= 570,
	.max_volt_perc_start		= 860,
	.cri_volt_dynslope		= 200,
	.low_volt_dynslope		= 65,
	.min_volt_dynslope		= 26,
	.mid_volt_dynslope		= 38,
	.med_volt_dynslope		= 70,
	.max_volt_dynslope		= 110,
};

static const struct sBattery_Parameters sBatParams_Rhodium300_1500mAh =
{
    .battery_capacity =			1500,
    .termination_current =		58,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3300,
    .low_volt_threshold =		3550,
    .min_volt_threshold =		3660,
    .mid_volt_threshold =		3760,
    .med_volt_threshold =		3810,
    .max_volt_threshold =		3990,
    .full_volt_threshold =		4190,
    .cri_volt_perc_start =		15,
    .low_volt_perc_start =		55,
    .min_volt_perc_start =		140,
    .mid_volt_perc_start =		295,
    .med_volt_perc_start =		515,
    .max_volt_perc_start =		821,
    .cri_volt_dynslope =		650,
    .low_volt_dynslope =		125,
    .min_volt_dynslope =		66,
    .mid_volt_dynslope =		23,
    .med_volt_dynslope =		57,
    .max_volt_dynslope =		115,
};

static const struct sBattery_Parameters sBatParams_Rhodium_1500mAh =
{
    .battery_capacity =			1500,
    .termination_current =		65,
    .temp_correction =			0,
    .temp_correction_const =		16,
    .volt_discharge_res_coeff =		27,
    .cri_volt_threshold =		3220,
    .low_volt_threshold =		3590,
    .min_volt_threshold =		3690,
    .mid_volt_threshold =		3770,
    .med_volt_threshold =		3870,
    .max_volt_threshold =		4050,
    .full_volt_threshold =		4130,
    .cri_volt_perc_start =		0,
    .low_volt_perc_start =		125,
    .min_volt_perc_start =		260,
    .mid_volt_perc_start =		550,
    .med_volt_perc_start =		710,
    .max_volt_perc_start =		925,
    .cri_volt_dynslope =		300,
    .low_volt_dynslope =		73,
    .min_volt_dynslope =		27,
    .mid_volt_dynslope =		63,
    .med_volt_dynslope =		83,
    .max_volt_dynslope =		120,
};

static const struct sBattery_Parameters sBatParams_Rhodium_2200mAh =
{
	.battery_capacity		= 2200,
	.termination_current		= 85,
	.temp_correction		= 0,
	.temp_correction_const		= 16,
	.volt_discharge_res_coeff	= 27,
	.cri_volt_threshold		= 3300,
	.low_volt_threshold		= 3600,
	.min_volt_threshold		= 3720,
	.mid_volt_threshold		= 3755,
	.med_volt_threshold		= 3820,
	.max_volt_threshold		= 4020,
	.full_volt_threshold		= 4180,
	.cri_volt_perc_start		= 10,
	.low_volt_perc_start		= 105,
	.min_volt_perc_start		= 260,
	.mid_volt_perc_start		= 390,
	.med_volt_perc_start		= 570,
	.max_volt_perc_start		= 860,
	.cri_volt_dynslope		= 200,
	.low_volt_dynslope		= 65,
	.min_volt_dynslope		= 26,
	.mid_volt_dynslope		= 38,
	.med_volt_dynslope		= 70,
	.max_volt_dynslope		= 110,
};

/*  Values for photon (valitated by photon community) */
static const struct sBattery_Parameters sBatParams_Photon_1200mAh =
{
    .battery_capacity =			1200,
    .termination_current =		36, //3% of batt capacity, but we will never charge until there
    .temp_correction =			0,	//not used
    .temp_correction_const =		26, //correction for low temp
    .volt_discharge_res_coeff =		27,	//correction for discharge	
    .cri_volt_threshold =		3300,	
    .low_volt_threshold =		3600,
    .min_volt_threshold =		3670,
    .mid_volt_threshold =		3750,
    .med_volt_threshold =		3815,
    .max_volt_threshold =		3950,
    .full_volt_threshold =		4100,
    .cri_volt_perc_start =		15,
    .low_volt_perc_start =		50,
    .min_volt_perc_start =		100,
    .mid_volt_perc_start =		300,
    .med_volt_perc_start =		570,
    .max_volt_perc_start =		765,
    .cri_volt_dynslope =		860,
    .low_volt_dynslope =		140,
    .min_volt_dynslope =		40,
    .mid_volt_dynslope =		24,
    .med_volt_dynslope =		69,
    .max_volt_dynslope =		64,
};


/* These values are the orginal from this files. They are present in Raphael battdrvr.dll ver.5.8.0.0 */
static const struct sBattery_Parameters* sBatParams_kovsky[] =
{
	&sBatParams_Kovsky_1500mAh,            /* batt_vendor = 1 */
	&sBatParams_Kovsky_2000mAh,            /* batt_vendor = 2 */
};

static const struct sBattery_Parameters* sBatParams_rhodium[] =
{
	&sBatParams_Rhodium_1500mAh,            /* batt_vendor = 1 */
	&sBatParams_Rhodium_2200mAh,            /* batt_vendor = 2 */
	&sBatParams_Rhodium300_1500mAh,         /* batt_vendor = 1 + VREF=1254 + HWBOARDID=80*/
};

/* These values are the orginal from this files. They are present in Raphael battdrvr.dll ver.5.8.0.0 */
static const struct sBattery_Parameters* sBatParams_diamond[] =
{
	&sBatParams_1340mAh,            /* batt_vendor = 1 */
	&sBatParams_900mAh,             /* batt_vendor = 2 */
};

/* Extracted from battdrvr.dll ver.2008.6.22.0 of HTC topaz */
static const struct sBattery_Parameters* sBatParams_topaz[] =
{
	&sBatParams_Topaz_1100mAh_v1,   /* batt_vendor = 1 */
	&sBatParams_Topaz_1100mAh_v2,   /* batt_vendor = 2 */
	&sBatParams_Topaz_1350mAh_v3,   /* batt_vendor = 3 */
	&sBatParams_Topaz_1350mAh_v4,   /* batt_vendor = 4 */
	&sBatParams_Topaz_2150mAh,      /* batt_vendor = 5 */
	/* Batt vendor > 5 are redirected to sBatParams_Topaz_1100mAh_v2 */
};

/* Extracted from battdrvr.dll ver.5.8.0.0 of HTC Raphael */
static const struct sBattery_Parameters* sBatParams_raphael[] =
{
	&sBatParams_Raphael_1800mAh,   /* batt_vendor = 1 */
	&sBatParams_1340mAh,   /* batt_vendor = 2 */
};

//camel - added for blackstone
/* Got from log in WM via kernelog testings  */
static const struct sBattery_Parameters* sBatParams_blackstone[] =
{
	&sBatParams_Blackstone_1500mAh, /* batt_vendor = 1 */
	&sBatParams_Blackstone_1350mAh, /* batt_vendor = 2 default */
};

static const struct sBattery_Parameters* sBatParams_photon[] =
{
	&sBatParams_Photon_1200mAh,           /* batt_vendor = 1 */
};

static const struct sBattery_Parameters* sBatParams_generic[] =
{
	&sBatParams_1800mAh,           /* batt_vendor = 1 */
	&sBatParams_1340mAh,           /* batt_vendor = 2 */
};

