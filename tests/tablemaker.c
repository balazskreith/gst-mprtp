#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <pcap.h>

typedef struct{
  gdouble avg;
  gdouble std;
}Cell;

typedef struct{
  Cell gp,fec,lr,ffre,nlf,owd;
}Stat;

typedef struct{
  gdouble sum;
  gdouble sumsq;
}CalcHelper;

typedef struct{

  Stat fractal50;
  Stat fractal100;
  Stat fractal300;

  Stat scream50;
  Stat scream100;
  Stat scream300;

}Flow;

static void _calc_stat_helper(FILE* fp, Stat* stat, gint* error){
  CalcHelper gph = {0.,0.};
  CalcHelper fech = {0.,0.};
  CalcHelper lrh = {0.,0.};
  CalcHelper ffreh = {0.,0.};
  CalcHelper nlfh = {0.,0.};
  CalcHelper owdh = {0.,0.};
  gdouble gp,fec,lr,ffre,owd;
  gint nlf;
  guint count = 0;
  gchar line[256];

  //read while file
  while (fgets(line, 1024, fp)){
    sscanf(line, "%lf,%lf,%lf,%lf,%d,%lf", &gp, &fec, &lr, &ffre, &nlf, &owd);
    ++count;

    //check for nan
    if(gp != gp || owd != owd || lr != lr || ffre != ffre){
      *error = 1;
    }
//    printf("%f,%f,%f,%f,%d,%f\n", gp, fec, lr, ffre, nlf, owd);
    gp/=125.;
    fec/=125.;
    lr*=100.;
    owd/=1000.;
    gph.sum += gp;
    gph.sumsq += gp*gp;
    fech.sum += fec;
    fech.sumsq += fec*fec;
    lrh.sum += lr;
    lrh.sumsq += lr*lr;
    ffreh.sum += ffre;
    ffreh.sumsq += ffre*ffre;
    nlfh.sum += nlf;
    nlfh.sumsq += nlf*nlf;
    owdh.sum += owd;
    owdh.sumsq += owd*owd;

  }



  stat->gp.avg = gph.sum / (gdouble) count;
  stat->gp.std = sqrt(gph.sumsq / (gdouble) count - (stat->gp.avg * stat->gp.avg));
  stat->fec.avg = fech.sum / (gdouble) count;
  stat->fec.std = sqrt(fech.sumsq / (gdouble) count - (stat->fec.avg * stat->fec.avg));
  stat->lr.avg = lrh.sum / (gdouble) count;
  stat->lr.std = sqrt(lrh.sumsq / (gdouble) count - (stat->lr.avg * stat->lr.avg));
  stat->ffre.avg = ffreh.sum / (gdouble) count;
  stat->ffre.std = sqrt(ffreh.sumsq / (gdouble) count - (stat->ffre.avg * stat->ffre.avg));
  stat->nlf.avg = nlfh.sum / (gdouble) count;
  stat->nlf.std = sqrt(nlfh.sumsq / (gdouble) count - (stat->nlf.avg * stat->nlf.avg));
  stat->owd.avg = owdh.sum / (gdouble) count;
  stat->owd.std = sqrt(owdh.sumsq / (gdouble) count - (stat->owd.avg * stat->owd.avg));
}

static void _calc_stat(Stat* stat, const gchar* path, const gchar *filename){
  gchar file[1024];
  gint error = 0;
  FILE* fp;
  memset(file, 0, 1024);
  sprintf(file, "%s/%s", path, filename);
  if(!(fp = fopen(file, "r"))){
    g_print("%s not exits\n", file);
    return;
  }
  _calc_stat_helper(fp, stat, &error);
  if(error == 1){
    g_print("nan is detected at %s\n", filename);
  }
  fclose(fp);
}

static void _calc_flow(Flow* flow, const gchar *path, const gchar* prefix){
  gchar filename[1024];
  sprintf(filename, "%s%s", prefix,"fractal_50ms.csv");
  _calc_stat(&flow->fractal50, path, filename);

  sprintf(filename, "%s%s", prefix,"fractal_100ms.csv");
  _calc_stat(&flow->fractal100, path, filename);

  sprintf(filename, "%s%s", prefix,"fractal_300ms.csv");
  _calc_stat(&flow->fractal300, path, filename);

  sprintf(filename, "%s%s", prefix,"scream_50ms.csv");
  _calc_stat(&flow->scream50, path, filename);

  sprintf(filename, "%s%s", prefix,"scream_100ms.csv");
  _calc_stat(&flow->scream100, path, filename);

  sprintf(filename, "%s%s", prefix,"scream_300ms.csv");
  _calc_stat(&flow->scream300, path, filename);
}
#define fprintln(fp, str) fprintf(fp, str"\n");
#define fprintfln(fp, str, ...) fprintf(fp, str"\n", __VA_ARGS__);
static void _fprintf_single_flow(FILE *fp, Flow* flow)
{
  fprintln(fp, "\\begin{table}");
  fprintln(fp, "\\centering" );
  fprintln(fp, "\\begin{tabular}{llll}" );
  fprintln(fp, "\\textbf{} & \\textbf{} & \\textbf{\\FRACTaL} & \\textbf{SCReAM} \\\\" );
  fprintln(fp, "\\hline" );
  fprintfln(fp, "\\multirow{3}{*}{\\rotatebox{90}{\\textbf{50ms}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal50.gp.avg, flow->fractal50.gp.std, flow->scream50.gp.avg, flow->scream50.gp.std);
  fprintfln(fp, " & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow->fractal50.lr.avg, flow->fractal50.lr.std, flow->scream50.lr.avg, flow->scream50.lr.std);
  fprintfln(fp, " & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow->fractal50.nlf.avg, flow->fractal50.nlf.std, flow->scream50.nlf.avg, flow->scream50.nlf.std);
  fprintfln(fp, " & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
      flow->fractal50.fec.avg, flow->fractal50.fec.std);
  fprintfln(fp, " & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
      flow->fractal50.ffre.avg, flow->fractal50.ffre.std);
  fprintfln(fp, " & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal50.owd.avg, flow->fractal50.owd.std, flow->scream50.owd.avg, flow->scream50.owd.std);
  fprintln(fp, " \\hline" );
  fprintfln(fp, "\\multirow{3}{*}{\\rotatebox{90}{\\textbf{100ms}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal100.gp.avg, flow->fractal100.gp.std, flow->scream100.gp.avg, flow->scream100.gp.std);
  fprintfln(fp, " & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal100.lr.avg, flow->fractal100.lr.std, flow->scream100.lr.avg, flow->scream100.lr.std);
  fprintfln(fp, " & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
      flow->fractal100.fec.avg, flow->fractal100.fec.std);
  fprintfln(fp, " & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
      flow->fractal100.ffre.avg, flow->fractal100.ffre.std);
  fprintfln(fp, " & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal100.owd.avg, flow->fractal100.owd.std, flow->scream100.owd.avg, flow->scream100.owd.std);
  fprintfln(fp, " & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal100.nlf.avg, flow->fractal100.nlf.std, flow->scream100.nlf.avg, flow->scream100.nlf.std);
  fprintln(fp, " \\hline" );
  fprintfln(fp, "\\multirow{3}{*}{\\rotatebox{90}{\\textbf{300ms}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal300.gp.avg, flow->fractal300.gp.std, flow->scream300.gp.avg, flow->scream300.gp.std);
  fprintfln(fp, " & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal300.lr.avg, flow->fractal300.lr.std, flow->scream300.lr.avg, flow->scream300.lr.std);
  fprintfln(fp, " & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
      flow->fractal300.fec.avg, flow->fractal300.fec.std);
  fprintfln(fp, " & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
      flow->fractal300.ffre.avg, flow->fractal300.ffre.std);
  fprintfln(fp, " & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow->fractal300.owd.avg, flow->fractal300.owd.std, flow->scream300.owd.avg, flow->scream300.owd.std);
  fprintfln(fp, " & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow->fractal300.nlf.avg, flow->fractal300.nlf.std, flow->scream300.nlf.avg, flow->scream300.nlf.std);
  fprintln(fp, "\\end{tabular}" );
  fprintln(fp, "\\caption{" );
  fprintln(fp, "TODO: CAPTION" );
  fprintln(fp, "}" );
  fprintln(fp, "\\label{table:rmcatX}" );
  fprintln(fp, "\\end{table}" );
}

static void _fprintf_two_flows(FILE* fp, Flow* flow1, Flow* flow2)
{
  fprintln(fp, "\\begin{table}");
  fprintln(fp, "\\centering");
  fprintln(fp, "\\begin{tabular}{lllll}");
  fprintln(fp, "&  &  & \\multicolumn{1}{c}{\\textbf{\\FRACTaL}} & \\multicolumn{1}{c}{\\textbf{SCReAM}} \\\\");
  fprintln(fp, " \\hline");
  fprintfln(fp, "\\multirow{6}{*}{\\rotatebox{90}{\\textbf{50ms}}} & \\multirow{3}{*}{\\rotatebox{90}{\\textbf{Flow 1}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow1->fractal50.gp.avg, flow1->fractal50.gp.std, flow1->scream50.gp.avg, flow1->scream50.gp.std);
  fprintfln(fp, " &  & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow1->fractal50.lr.avg, flow1->fractal50.lr.std, flow1->scream50.lr.avg, flow1->scream50.lr.std);
  fprintfln(fp, " &  & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow1->fractal50.nlf.avg, flow1->fractal50.nlf.std, flow1->scream50.nlf.avg, flow1->scream50.nlf.std);
  fprintfln(fp, " &  & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
      flow1->fractal50.fec.avg, flow1->fractal50.fec.std);
  fprintfln(fp, " &  & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
      flow1->fractal50.ffre.avg, flow1->fractal50.ffre.std);
  fprintfln(fp, " &  & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow1->fractal50.owd.avg, flow1->fractal50.owd.std, flow1->scream50.owd.avg, flow1->scream50.owd.std);

  fprintfln(fp, " & \\multirow{3}{*}{\\rotatebox{90}{\\textbf{Flow 2}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow2->fractal50.gp.avg, flow2->fractal50.gp.std, flow2->scream50.gp.avg, flow2->scream50.gp.std);
  fprintfln(fp, " &  & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow2->fractal50.lr.avg, flow2->fractal50.lr.std, flow2->scream50.lr.avg, flow2->scream50.lr.std);
  fprintfln(fp, " &  & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow2->fractal50.nlf.avg, flow2->fractal50.nlf.std, flow2->scream50.nlf.avg, flow2->scream50.nlf.std);
  fprintfln(fp, " &  & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
      flow2->fractal50.fec.avg, flow2->fractal50.fec.std);
  fprintfln(fp, " &  & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
      flow2->fractal50.ffre.avg, flow2->fractal50.ffre.std);
  fprintfln(fp, " &  & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow2->fractal50.owd.avg, flow2->fractal50.owd.std, flow2->scream50.owd.avg, flow2->scream50.owd.std);
  fprintln(fp, " \\hline");

  fprintfln(fp, "\\multirow{6}{*}{\\rotatebox{90}{\\textbf{50ms}}} & \\multirow{3}{*}{\\rotatebox{90}{\\textbf{Flow 1}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow1->fractal100.gp.avg, flow1->fractal100.gp.std, flow1->scream100.gp.avg, flow1->scream100.gp.std);
    fprintfln(fp, " &  & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
          flow1->fractal100.lr.avg, flow1->fractal100.lr.std, flow1->scream100.lr.avg, flow1->scream100.lr.std);
    fprintfln(fp, " &  & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
          flow1->fractal100.nlf.avg, flow1->fractal100.nlf.std, flow1->scream100.nlf.avg, flow1->scream100.nlf.std);
    fprintfln(fp, " &  & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
        flow1->fractal100.fec.avg, flow1->fractal100.fec.std);
    fprintfln(fp, " &  & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
        flow1->fractal100.ffre.avg, flow1->fractal100.ffre.std);
    fprintfln(fp, " &  & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow1->fractal100.owd.avg, flow1->fractal100.owd.std, flow1->scream100.owd.avg, flow1->scream100.owd.std);

    fprintfln(fp, " & \\multirow{3}{*}{\\rotatebox{90}{\\textbf{Flow 2}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow2->fractal100.gp.avg, flow2->fractal100.gp.std, flow2->scream100.gp.avg, flow2->scream100.gp.std);
    fprintfln(fp, " &  & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
          flow2->fractal100.lr.avg, flow2->fractal100.lr.std, flow2->scream100.lr.avg, flow2->scream100.lr.std);
    fprintfln(fp, " &  & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
          flow2->fractal100.nlf.avg, flow2->fractal100.nlf.std, flow2->scream100.nlf.avg, flow2->scream100.nlf.std);
    fprintfln(fp, " &  & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
        flow2->fractal100.fec.avg, flow2->fractal100.fec.std);
    fprintfln(fp, " &  & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
        flow2->fractal100.ffre.avg, flow2->fractal100.ffre.std);
    fprintfln(fp, " &  & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow2->fractal100.owd.avg, flow2->fractal100.owd.std, flow2->scream100.owd.avg, flow2->scream100.owd.std);
    fprintln(fp, " \\hline");

    fprintfln(fp, "\\multirow{6}{*}{\\rotatebox{90}{\\textbf{50ms}}} & \\multirow{3}{*}{\\rotatebox{90}{\\textbf{Flow 1}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow1->fractal300.gp.avg, flow1->fractal300.gp.std, flow1->scream300.gp.avg, flow1->scream300.gp.std);
    fprintfln(fp, " &  & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow1->fractal300.lr.avg, flow1->fractal300.lr.std, flow1->scream300.lr.avg, flow1->scream300.lr.std);
    fprintfln(fp, " &  & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow1->fractal300.nlf.avg, flow1->fractal300.nlf.std, flow1->scream300.nlf.avg, flow1->scream300.nlf.std);
    fprintfln(fp, " &  & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
      flow1->fractal300.fec.avg, flow1->fractal300.fec.std);
    fprintfln(fp, " &  & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
      flow1->fractal300.ffre.avg, flow1->fractal300.ffre.std);
    fprintfln(fp, " &  & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow1->fractal300.owd.avg, flow1->fractal300.owd.std, flow1->scream300.owd.avg, flow1->scream300.owd.std);

    fprintfln(fp, " & \\multirow{3}{*}{\\rotatebox{90}{\\textbf{Flow 2}}} & \\textbf{GP {[}kbps{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow2->fractal300.gp.avg, flow2->fractal300.gp.std, flow2->scream300.gp.avg, flow2->scream300.gp.std);
    fprintfln(fp, " &  & \\textbf{LR {[}\\%{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow2->fractal300.lr.avg, flow2->fractal300.lr.std, flow2->scream300.lr.avg, flow2->scream300.lr.std);
    fprintfln(fp, " &  & \\textbf{NoLF} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
        flow2->fractal300.nlf.avg, flow2->fractal300.nlf.std, flow2->scream300.nlf.avg, flow2->scream300.nlf.std);
    fprintfln(fp, " &  & \\textbf{FEC {[}kbs{]}} & %4.2f $\\pm$ %4.2f  & - \\\\",
      flow2->fractal300.fec.avg, flow2->fractal300.fec.std);
    fprintfln(fp, " &  & \\textbf{FFRE} & %4.2f $\\pm$ %4.2f & - \\\\" ,
      flow2->fractal300.ffre.avg, flow2->fractal300.ffre.std);
    fprintfln(fp, " &  & \\textbf{OWD {[}ms{]}} & %4.2f $\\pm$ %4.2f & %4.2f $\\pm$ %4.2f \\\\",
      flow2->fractal300.owd.avg, flow2->fractal300.owd.std, flow2->scream300.owd.avg, flow2->scream300.owd.std);

  fprintln(fp, "\\end{tabular}");
  fprintln(fp, "\\caption{");
  fprintln(fp, "TODO: CAPTION!!!");
  fprintln(fp, "}");
  fprintln(fp, "\\label{table:rmcatX}");
  fprintln(fp, "\\end{table}");


}

int main (int argc, char **argv)
{
  FILE *fp;
  Flow* flows;
  gint flownum;
  if(argc < 3){
//  usage:
    g_print("Usage: ./program flownum datstats_dir result_path\n");
    return 0;
  }
  flows = g_malloc0(sizeof(Flow) * 3);
  fp = fopen (argv[3],"w");
  flownum = atoi(argv[1]);
  if(flownum == 1){
    _calc_flow(flows, argv[2], "datstat_");
    _fprintf_single_flow(fp, flows);
  }else if(flownum == 2){
    _calc_flow(flows,     argv[2], "datstat_flow1_");
    _calc_flow(flows + 1, argv[2], "datstat_flow2_");
    _fprintf_two_flows(fp, flows, flows + 1);
  }
  g_free(flows);
  fclose(fp);
  g_print("Results are made in %s\n", argv[3]);
  return 0;
}
