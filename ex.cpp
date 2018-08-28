#include<systemc.h>
#include<string>
#include<vector>
#include<map>
#include<fstream>

using std::string;
using std::vector;
using std::map;
using std::fstream;

class write_if_fila: virtual public sc_interface
{
	public:
		virtual void write(string) = 0;
		virtual void nb_write(string) = 0;
};

class read_if_fila: virtual public sc_interface
{
	public:
		virtual void read(string &) = 0;
		virtual void nb_read(string &) = 0;
		virtual void notify() = 0;
};

class write_if: virtual public sc_interface
{
	public:
		virtual void write(string) = 0;
};

class read_if: virtual public sc_interface
{
	public:
		virtual void read(string &) = 0;
};

class bus: public sc_channel, public write_if, public read_if
{
	public:
		bus(sc_module_name name): sc_channel(name){stamp = sc_time(-1,SC_NS);}
		void write(string p)
		{
			while(sc_time_stamp() == stamp)
				wait(sc_time(1,SC_NS));
			stamp = sc_time_stamp();
			palavra = p;
			write_event.notify();
		}
		const sc_event& default_event() const
		{
			return write_event;
		}
		void read(string &p)
		{
			p = palavra;
		}
	private:
		string palavra;
		sc_event write_event;
		sc_time stamp;
};

class cons_bus: public sc_channel, public write_if_fila, public read_if_fila
{
	public:
		cons_bus(sc_module_name name): sc_channel(name){}		
		void write(string p)
		{
			palavra = p;
			write_event.notify(SC_ZERO_TIME);
			wait(read_event);
		}
		void nb_write(string p)
		{
			palavra = p;
			//write_event.notify(SC_ZERO_TIME);
		}
		void read(string &p)
		{
			if(palavra != " ")
			{
				p = palavra;
				read_event.notify(SC_ZERO_TIME);
				palavra = " ";
			}
			else
				wait(write_event);
		}
		void nb_read(string &p)
		{
			if(palavra != " ")
			{
				p  = palavra;
				palavra = " ";
			}
		}
		void notify(){read_event.notify(SC_ZERO_TIME);}
		const sc_event& default_event() const
		{
			return write_event;
		}
	private:
		string palavra;
		sc_event write_event;
		sc_event read_event;
};

/*
	ESTRUTURA DE INSTRUÇÃO
	DADD rd, rs, rt
	Rx, valor

*/

class instruction_queue: public sc_module
{
	public:
		sc_in_clk clock;
		sc_port<write_if_fila> out;
		vector<string> instruct_queue;
		SC_HAS_PROCESS(instruction_queue);
		instruction_queue(sc_module_name name, vector<string> inst_q): sc_module(name), instruct_queue(inst_q)
		{
			SC_THREAD(main);
			sensitive << clock.pos();
			dont_initialize();
		}
		void main()
		{
			for(unsigned int i = 0 ; i < instruct_queue.size() ; i++)
			{
				out->write(instruct_queue[i]);
				wait();
			}
		}
};

/*
	ESTRUTURA DO REG_BANK (IN)
	R/W S/V REGISTER_NUMBER VALUE

	ESTRUTURA DO REG_BANK(CDB_IN)
	CDB_INDEX VALUE

	ESTRUTURA DO REG_BANK (OUT)
	VALUE
*/
class register_bank: public sc_module
{
	public:
		sc_port<read_if_fila> in;
		sc_port<write_if_fila> out;
		sc_port<read_if> in_cdb;
		SC_HAS_PROCESS(register_bank);
		register_bank(sc_module_name name,vector<int> rg_values): sc_module(name), reg_values(rg_values)
		{
			reg_status.resize(32);
			reg_status.assign(32,0);
			SC_THREAD(le_bus);
			sensitive << in;
			dont_initialize();
			SC_THREAD(le_cdb);
			sensitive << in_cdb;
			dont_initialize();
		}
		void le_bus()
		{
			vector<string> ord;
			unsigned int index;
			string p;
			while(true)
			{
				in->read(p);
				ord = instruction_split(p);
				index = std::stoi(ord[2]);
				if(ord[0] == "R")
				{
					if(ord[1] == "S")
						out->nb_write(std::to_string(reg_status[index]));
					else
						out->nb_write(std::to_string(reg_values[index]));
				}
				else
				{
					if(ord[1] == "S")
						reg_status[index] = std::stof(ord[3]);
					else
						reg_values[index] = std::stof(ord[3]);
				}
				wait();
			}
		}

		void le_cdb()
		{
			int rs_index;
			string p;
			float value;
			vector<string> ord;
			while(true)
			{
				in_cdb->read(p);
				ord = instruction_split(p);
				rs_index = std::stoi(ord[0]);
				value = std::stof(ord[1]);
				for(unsigned int i = 0 ; i < reg_status.size() ; i++)
					if(reg_status[i] == rs_index)
					{
						reg_status[i] = 0;
						reg_values[i] = value;
						cout << "Valor de R" << i << " atualizado para " << value << endl << flush;
					}
				wait();
			}
		}

		vector<string> instruction_split(string p)
		{
			vector<string> ord;
			unsigned int i,last_pos;
			last_pos = 0;
			for(i = 0 ; i < p.size() ; i++)
				if(p[i] == ' ')
				{
					ord.push_back(p.substr(last_pos,i-last_pos));
					last_pos = i+1;
				}
			ord.push_back(p.substr(last_pos,p.size()-last_pos));
			return ord;
		}
	private:
		vector<int> reg_status;
		vector<int> reg_values;
};

class res_station: public sc_module
{
	public:
		int id;
		bool Busy;
		string op;
		int vj,vk,qj,qk;
		map<string,int> instruct_time;
		sc_port<write_if> out;
		sc_port<read_if> in;
		sc_event exec_event;
		SC_HAS_PROCESS(res_station);
		res_station(sc_module_name name, map<string,int> inst_map,int i,sc_event *f/*, vector<int> *reg1, vector<int> *reg2*/): 
										sc_module(name), id(i), instruct_time(inst_map),fila(f)//, reg_values(reg1), reg_status(reg2)
		{
			Busy = false;
			SC_THREAD(exec);
			sensitive << exec_event;
			dont_initialize();
			SC_METHOD(leitura);
			sensitive << in;
			dont_initialize();
		}
		void exec()
		{
			while(true)
			{
				//Enquanto houver dependencia de valor em outra RS, espere
				while(qj || qk)
					wait(val_enc);
				float res = 0;
				if(op.substr(0,4) == "DADD")
					res = vj + vk;
				else if(op.substr(0,4) == "DSUB")
					res = vj - vk;
				else if(op.substr(0,4) == "DMUL")
					res = vj*vk;
				else if(op.substr(0,4) == "DDIV")
				{
					if(vk)
						res = vj/vk;
					else
						cout << "Divisao por 0, instrucao ignorada!" << endl;
				}
				cout << "Execuçao da instruçao " << op << " iniciada no ciclo " << sc_time_stamp() << " em " << name() << endl << flush;
				cout << vj << ' ' << vk << endl;
				wait(sc_time(instruct_time[op],SC_NS));
				cout << "**Instrucao " << op << " completada no ciclo " << sc_time_stamp() << " em " << name() << " com resultado " << res << endl << flush;
				Busy = false;
				string escrita_saida = std::to_string(id) + ' ' + std::to_string(res);
				out->write(escrita_saida);
				(*fila).notify(sc_time(1,SC_NS));
				wait();
			}
		}
		void leitura()
		{
			if(qj || qk)
			{
				unsigned int i;
				int rs_source;
				in->read(p);
				//cout << "////" << p << endl << flush;
				for(i = 0 ; i < p.size() && p[i] != ' '; i++)
					;
				rs_source = std::stoi(p.substr(0,i));
				if(qj == rs_source)
				{
					qj = 0;
					vj = std::stoi(p.substr(i+1,p.size() - i - 1));
					cout << "Instrucao " << op << " conseguiu o valor " << vj << " da RS_" << rs_source << endl << flush; 
					val_enc.notify(sc_time(1,SC_NS));
				}
				if(qk == rs_source)
				{
					qk = 0;
					vk = std::stoi(p.substr(i+1,p.size() - i - 1));
					cout << "Instrucao " << op << " conseguiu o valor " << vk << " da RS_" << rs_source << endl << flush; 
					val_enc.notify(sc_time(1,SC_NS));
				}
			}
		}
	private:
		string p;
		sc_event *fila;
		sc_event val_enc;
		//vector<int> *reg_values;
		//vector<int> *reg_status;
};

class res_vector: public sc_module
{
	public:
		vector<res_station *> rs;
		sc_port<read_if_fila> in_fila;
		sc_port<read_if> in_cdb;
		sc_port<write_if> out_cdb;
		sc_port<read_if_fila> rb_in;
		sc_port<write_if_fila> rb_out;
		sc_event rs_livre;
		SC_HAS_PROCESS(res_vector);
		res_vector(sc_module_name name, unsigned int t1, unsigned int t2, unsigned int t3,map<string,int> instruct_time,
			vector<int> mem): sc_module(name),adder_tam(t1),multiplier_tam(t2),memory_tam(t3),memoria(mem)
		{
			unsigned int tam = adder_tam + multiplier_tam + memory_tam;
			res_type = {{"DADD",0},{"DADDI",0},{"DSUB",0},{"DSUBI",0},{"DMUL",1},{"DMULI",1},{"DDIV",1},{"DDIVI",1},{"L.D",2},{"S.D",2}};
			start[0] = 0;
			start[1] = start[0] + multiplier_tam;
			start[2] = start[1] + memory_tam;
			start[4] = tam;
			rs.resize(tam);
			string texto;
			for(unsigned int i = 0 ; i < start[0]+start[1]+start[2] ; i++)
			{
				if(i < start[1])
					texto = "Add" + std::to_string(i-start[0]+1);
				else if(i < start[2])
					texto = "Mul" + std::to_string(i-start[1]+1);
				else
					texto = "Load" + std::to_string(i-start[2]+1);
				rs[i] = new res_station(texto.c_str(),instruct_time,i+1,&rs_livre);
				rs[i]->in(in_cdb);
				rs[i]->out(out_cdb);
			}
			SC_THREAD(leitura_fila);
			sensitive << in_fila;
			dont_initialize();
		}
		void leitura_fila()
		{
			while(true)
			{
				vector<string> ord;
				int pos,reg1v,reg2v,regstv,regst;
				in_fila->nb_read(p); //le sem notificar ao canal que leu
				ord = instruction_split(p);
				pos = busy_check(ord[0]);
				if(pos == -1)
				{
					cout << "//Todas as estacoes ocupadas no ciclo " << sc_time_stamp() << endl << flush;
					wait(rs_livre);
					pos = busy_check(ord[0]);
				}
				in_fila->notify(); //notifica ao canal que leu
				cout << "Issue da instrução " << ord[0] << " no ciclo " << sc_time_stamp() << " para a rs_" << pos << endl << flush;
				rs[pos]->op = ord[0];
				regstv = std::stoi(ord[1].substr(1,ord[1].size()-1));
				reg1v = std::stoi(ord[2].substr(1,ord[2].size()-1));
				reg2v = std::stoi(ord[3].substr(1,ord[3].size()-1));
				//reg_status[regstv] = pos+1; //muda o valor do status do registrador destino para a ultima RS que ira escrever nele
				ask_status(false,regstv,pos+1);
				regst = ask_status(true,reg1v);
				if(regst == 0) 			// Se nenhuma RS ira escrever no registrador,
					rs[pos]->vj = ask_value(reg1v);
					//rs[pos]->vj = reg_values[reg1v]; 	// use o valor dele
				else
				{
					cout << "instruçao " << ord[0] << " aguardando reg R" << reg1v << endl << flush;
					//rs[pos]->qj = reg_status[reg1v];// Senao, preencha a RS que guarda o valor que sera escrito
					rs[pos]->qj = regst;
				}
				regst = ask_status(true,reg2v);
				if(ord[0].at(ord[0].size() - 1) == 'I')
					rs[pos]->vk = std::stoi(ord[3]);
				else if(regst == 0) 			// Se nenhuma RS ira escrever no registrador,
					rs[pos]->vk = ask_value(reg2v);
					//rs[pos]->vk = reg_values[reg2v]; 	// use o valor dele
				else
				{
					rs[pos]->qk = regst;
					//rs[pos]->qk = reg_status[reg2v]; //Senao, preencha a RS que guarda o valor que sera escrito
					cout << "instruçao " << ord[0] << " aguardando reg R" << reg2v << endl << flush;
				}
				rs[pos]->Busy = true;
				rs[pos]->exec_event.notify(sc_time(1,SC_NS));
				wait();
			}
		}
	private:
		string p;
		unsigned int start[4]; //Guarda o indice onde inicia a instrucao de cada tipo, sendo que o 4o elemento guarda apenas o numero de rs (soma de todos os indices)
		unsigned int adder_tam, multiplier_tam, memory_tam;
		map<string,short int> res_type;
		vector<int> memoria;
		int busy_check(string inst)
		{
			short int inst_type = res_type[inst];
			for(unsigned int i = start[inst_type] ; i < start[inst_type + 1] ; i++)
				if(!rs[i]->Busy)
					return i;
			return -1;
		}
		float ask_value(unsigned int index)
		{
			string res;
			rb_out->write("R V " + std::to_string(index));
			rb_in->read(res);
			return std::stof(res);
		}
		unsigned int ask_status(bool read,unsigned int index,unsigned int pos = 0)
		{
			string res;
			if(read)
			{
				rb_out->write("R S " + std::to_string(index));
				rb_in->read(res);
				return std::stoi(res);
			}
			else
				rb_out->write("W S " + std::to_string(index) + " " + std::to_string(pos));
			return 0;
		}
		vector<string> instruction_split(string p)
		{
			vector<string> ord;
			unsigned int i,last_pos;
			i = last_pos = 0;
			for(i = 0 ; i < p.size() && p[i] != ' ' ; i++);
			ord.push_back(p.substr(0,i));
			last_pos = i;
			for(; i < p.size() ; i++)
				if(p[i] == ',')
				{
					ord.push_back(p.substr(last_pos+1,i-last_pos-1));
					last_pos = i;
				}
			ord.push_back(p.substr(last_pos+1,p.size()-last_pos-1));
			return ord;
		}
};

class top: public sc_module
{
	public:
		sc_in_clk clock;
		res_vector *rst;
		register_bank *rb;
		bus *CDB;
		cons_bus *inst_bus;
		cons_bus *rb_bus;
		instruction_queue *fila;
	top(sc_module_name name,unsigned int t1, unsigned int t2,unsigned int t3,map<string,int> instruct_time,
		vector<string> instruct_queue, vector<int> reg_status,vector<int> memoria): sc_module(name)
	{
		CDB = new bus("CDB");
		inst_bus = new cons_bus("inst_bus");
		rb_bus = new cons_bus("rb_bus");
		fila = new instruction_queue("fila_inst",instruct_queue);
		fila->clock(clock);
		fila->out(*inst_bus);
		rst = new res_vector("rs_vc",t1,t2,t3,instruct_time,memoria);
		rb = new register_bank("register_bank",reg_status);
		rst->in_fila(*inst_bus);
		rst->in_cdb(*CDB);
		rst->out_cdb(*CDB);
		rst->rb_in(*rb_bus);
		rst->rb_out(*rb_bus);
		rb->in(*rb_bus);
		rb->out(*rb_bus);
		rb->in_cdb(*CDB);
	}
};

int sc_main(int argc, char *argv[])
{
	map<string,int> instruct_time{{"DADD",4},{"DADDI",4},{"DSUB",6},{"DSUBI",6},{"DMUL",10},{"DMULI",10},{"DDIV",16},{"DDIVI",16}};
	sc_clock clock("clk");
	ifstream inFile;
	vector<string> instruction_queue;
	string line;
	vector<int> status,memoria;
	int value,i = 0;
	if(argc < 4)
	{
		cout << "Uso: ./ex.x <lista_instrucoes> <reg_valores_iniciais> <memoria_valores_iniciais>" << endl;
		return 1;
	}
	inFile.open(argv[1]);
	if(!inFile.is_open())
	{
		cerr << "Arquivo " << argv[1] << " nao existe!" << endl;
		return 1;
	}
	cout << "---------------------------------------------------" << endl;
	cout << "\t\tFILA DE INSTRUCOES" << endl;
	while(getline(inFile,line))
	{
		cout << line << endl;
		instruction_queue.push_back(line);
	}
	cout << "---------------------------------------------------" << endl << endl;
	inFile.close();
	inFile.open(argv[2]);
	if(!inFile.is_open())
	{
		cerr << "Arquivo " << argv[2] << " nao existe!" << endl;
		return 1;
	}
	cout << "---------------------------------------------------" << endl;
	cout << "VALOR INICIAL DOS REGISTRADORES" << endl;
	while(inFile >> value)
	{
		cout << 'R' << i << " = " << value << endl;
		status.push_back(value);
		i++;
	}
	cout << "---------------------------------------------------" << endl << endl;
	inFile.close();
	inFile.open(argv[3]);
	if(!inFile.is_open())
	{
		cerr << "Arquivo " << argv[3] << " nao existe!" << endl;
		return 1;
	}
	cout << "---------------------------------------------------" << endl;
	cout << "ESTADO INICIAL DA MEMORIA" << endl;
	i = 0;
	while(inFile >> value)
	{
		if(i%10 == 0)
			cout << endl << i << "\t||\t";
		cout << value << '\t';
		memoria.push_back(value);
		i++;
	}
	cout << endl << "---------------------------------------------------" << endl << endl;
	top top1("top",3,2,2,instruct_time,instruction_queue,status,memoria);
	top1.clock(clock);
	sc_start(100,SC_NS);
	return 0;
}
